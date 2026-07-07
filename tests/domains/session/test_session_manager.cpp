#include "domains/session/session_manager.hpp"

#include "core/error_event.hpp"
#include "core/event_bus.hpp"
#include "domains/audio/events.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/session/events.hpp"
#include "domains/session/service_state.hpp"
#include "domains/speech/events.hpp"
#include "domains/speech/speech_backend.hpp"
#include "domains/speech/whisper_backend.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <thread>
#include <variant>

using yoru::audio::RecordingManager;
using yoru::core::ErrorOccurred;
using yoru::core::EventBus;
using yoru::core::SessionId;
using yoru::session::ServiceState;
using yoru::session::SessionCancelled;
using yoru::session::SessionError;
using yoru::session::SessionManager;
using yoru::speech::Model;
using yoru::speech::SpeechBackend;
using yoru::speech::SpeechError;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionRequest;
using yoru::speech::TranscriptionResult;

namespace {

// A SpeechBackend that never touches whisper.cpp. SpeechBackend exists
// precisely so the recognition engine is substitutable (ADR-004); this is
// that substitution exercised for test determinism, since forcing a real
// WhisperBackend to fail on demand isn't practical. Unlike WhisperBackend,
// it does not publish TranscriptionStarted/Completed itself: that is a
// WhisperBackend implementation detail, not a SpeechBackend interface
// requirement, so SessionManager must not depend on a backend doing it.
class FakeSpeechBackend : public SpeechBackend {
public:
    std::optional<SpeechError> load_model(const Model&) override {
        return std::nullopt;
    }

    void unload_model() override {}

    bool has_model_loaded() const override {
        return true;
    }

    TranscriptionResult transcribe(SessionId /*session_id*/, const std::vector<float>& samples,
                                   const TranscriptionRequest& request) override {
        if (should_fail) {
            return SpeechError{"fake transcription failure"};
        }
        return Transcript{
            .text = "fake transcript",
            .detected_language = "en",
            .requested_language = request.language,
            .audio_duration =
                std::chrono::milliseconds{static_cast<std::int64_t>(samples.size()) * 1000 / 16000},
            .processing_time = std::chrono::milliseconds{1},
        };
    }

    TranscriptionResult transcribe_partial(const std::vector<float>& window,
                                           const TranscriptionRequest& request) override {
        return transcribe(SessionId{0}, window, request);
    }

    bool should_fail = false;
};

// Every test here that records needs a real capture device. Skip
// gracefully, as established for RecordingManager's own tests, rather
// than failing on a host without one.
bool skip_if_no_device(const std::optional<SessionError>& error) {
    if (error.has_value()) {
        MESSAGE("skipping: no capture device available (", error->message, ")");
        return true;
    }
    return false;
}

} // namespace

TEST_CASE("start_session() twice reports that a session is already active") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend);

    if (skip_if_no_device(manager.start_session())) {
        return;
    }

    const auto second_error = manager.start_session();
    REQUIRE(second_error.has_value());
    CHECK(manager.state() == ServiceState::Recording);

    manager.cancel_session();
}

TEST_CASE("stop_session() without an active session reports an error") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend);

    const auto result = manager.stop_session();

    REQUIRE(std::holds_alternative<SessionError>(result));
    CHECK(manager.state() == ServiceState::Idle);
}

TEST_CASE("cancel_session() when idle reports an error") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend);

    const auto error = manager.cancel_session();

    REQUIRE(error.has_value());
}

TEST_CASE("cancel_session() during recording releases the device and returns to idle") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend);

    if (skip_if_no_device(manager.start_session())) {
        return;
    }
    REQUIRE(manager.state() == ServiceState::Recording);

    std::optional<SessionId> cancelled_id;
    bus.subscribe<SessionCancelled>(
        [&](const SessionCancelled& event) { cancelled_id = event.session_id; });

    const auto error = manager.cancel_session();

    REQUIRE_FALSE(error.has_value());
    CHECK(manager.state() == ServiceState::Idle);
    CHECK_FALSE(recording_manager.is_recording());
    CHECK(cancelled_id.has_value());

    // The device was released, so a new session can start right away.
    CHECK_FALSE(skip_if_no_device(manager.start_session()));
    manager.cancel_session();
}

TEST_CASE("start then stop runs the full cycle and returns the Transcript") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend);

    if (skip_if_no_device(manager.start_session())) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    const auto result = manager.stop_session();

    CHECK(manager.state() == ServiceState::Idle);
    REQUIRE(std::holds_alternative<Transcript>(result));
    CHECK(std::get<Transcript>(result).text == "fake transcript");
}

TEST_CASE("a transcription failure transitions to Error, and cancel_session() recovers") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    speech_backend.should_fail = true;
    SessionManager manager(bus, recording_manager, speech_backend);

    std::optional<ErrorOccurred> error_event;
    bus.subscribe<ErrorOccurred>([&](const ErrorOccurred& event) { error_event = event; });

    if (skip_if_no_device(manager.start_session())) {
        return;
    }

    const auto stop_result = manager.stop_session();

    REQUIRE(std::holds_alternative<SessionError>(stop_result));
    CHECK(manager.state() == ServiceState::Error);
    REQUIRE(error_event.has_value());
    CHECK(error_event->component == "speech");

    // Recovery: cancel_session() acknowledges the error and returns to
    // Idle, able to accept a new session.
    REQUIRE_FALSE(manager.cancel_session().has_value());
    CHECK(manager.state() == ServiceState::Idle);

    speech_backend.should_fail = false;
    CHECK_FALSE(skip_if_no_device(manager.start_session()));
    manager.cancel_session();
}

TEST_CASE("end-to-end with a real whisper.cpp model produces a Transcript from real audio") {
    const std::filesystem::path model_path =
        "/usr/share/whisper.cpp-model-large-v3-turbo-q5_0/ggml-large-v3-turbo-q5_0.bin";
    if (!std::filesystem::exists(model_path)) {
        MESSAGE("skipping: large-v3-turbo model not installed (", model_path.string(), ")");
        return;
    }

    EventBus bus;
    RecordingManager recording_manager(bus);
    yoru::speech::WhisperBackend speech_backend(bus);
    REQUIRE_FALSE(speech_backend
                      .load_model(Model{
                          .name = "ggml-large-v3-turbo-q5_0",
                          .size = yoru::speech::ModelSize::Large,
                          .supported_language = "multi",
                          .path = model_path,
                          .backend = "whisper.cpp",
                      })
                      .has_value());

    SessionManager manager(bus, recording_manager, speech_backend);

    if (skip_if_no_device(manager.start_session())) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    const auto result = manager.stop_session();

    CHECK(manager.state() == ServiceState::Idle);
    REQUIRE(std::holds_alternative<Transcript>(result));
    CHECK(std::get<Transcript>(result).audio_duration > std::chrono::milliseconds{0});
}
