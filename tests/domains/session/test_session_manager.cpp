#include "domains/session/session_manager.hpp"

#include "core/error_event.hpp"
#include "core/event_bus.hpp"
#include "domains/audio/events.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/config/events.hpp"
#include "domains/session/events.hpp"
#include "domains/session/service_state.hpp"
#include "domains/speech/events.hpp"
#include "domains/speech/speech_backend.hpp"
#include "domains/speech/whisper_backend.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

using yoru::audio::RecordingManager;
using yoru::config::Configuration;
using yoru::core::ErrorOccurred;
using yoru::core::EventBus;
using yoru::core::SessionId;
using yoru::session::QueuedJob;
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

    TranscriptionResult transcribe(SessionId session_id, const std::vector<float>& samples,
                                   const TranscriptionRequest& request) override {
        {
            std::unique_lock lock(gate_mutex);
            last_request = request;
            transcribe_calls.push_back(session_id);
            gate_cv.wait(lock, [this] { return !gated || aborted; });
        }
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

    void request_abort() override {
        std::lock_guard lock(gate_mutex);
        aborted = true;
        gate_cv.notify_all();
    }

    // While gated, transcribe() blocks until open_gate() (or an abort).
    void close_gate() {
        std::lock_guard lock(gate_mutex);
        gated = true;
    }
    void open_gate() {
        std::lock_guard lock(gate_mutex);
        gated = false;
        gate_cv.notify_all();
    }

    std::optional<TranscriptionRequest> request_seen() {
        std::lock_guard lock(gate_mutex);
        return last_request;
    }

    std::vector<SessionId> calls() {
        std::lock_guard lock(gate_mutex);
        return transcribe_calls;
    }

    bool should_fail = false;

private:
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool gated = false;
    bool aborted = false;
    std::optional<TranscriptionRequest> last_request;
    std::vector<SessionId> transcribe_calls;
};

// Pumps poll_completed() until `count` results were published or time
// runs out, the way the main loop does.
bool pump_until(SessionManager& manager, std::size_t count) {
    std::size_t total = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (total < count && std::chrono::steady_clock::now() < deadline) {
        total += manager.poll_completed();
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return total >= count;
}

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
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

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
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    const auto result = manager.stop_session();

    REQUIRE(std::holds_alternative<SessionError>(result));
    CHECK(manager.state() == ServiceState::Idle);
}

TEST_CASE("cancel_session() when idle reports an error") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    const auto error = manager.cancel_session();

    REQUIRE(error.has_value());
}

TEST_CASE("cancel_session() during recording releases the device and returns to idle") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

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

TEST_CASE("start then stop queues the job and poll_completed() publishes the Transcript") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    std::optional<SessionId> started_id;
    std::optional<yoru::speech::TranscriptionCompleted> completed;
    bus.subscribe<yoru::speech::TranscriptionStarted>(
        [&](const yoru::speech::TranscriptionStarted& event) { started_id = event.session_id; });
    bus.subscribe<yoru::speech::TranscriptionCompleted>(
        [&](const yoru::speech::TranscriptionCompleted& event) { completed = event; });

    if (skip_if_no_device(manager.start_session())) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    const auto result = manager.stop_session();

    REQUIRE(std::holds_alternative<QueuedJob>(result));
    const auto job = std::get<QueuedJob>(result);
    CHECK(job.position == 0);
    REQUIRE(started_id.has_value());
    CHECK(*started_id == job.session_id);

    REQUIRE(pump_until(manager, 1));
    CHECK(manager.state() == ServiceState::Idle);
    REQUIRE(completed.has_value());
    CHECK(completed->session_id == job.session_id);
    CHECK(completed->transcript.text == "fake transcript");
    CHECK(manager.status().last_completed == job.session_id);
}

TEST_CASE("a second session can record while the first is still being transcribed") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    speech_backend.close_gate();
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    std::vector<SessionId> completed_ids;
    bus.subscribe<yoru::speech::TranscriptionCompleted>(
        [&](const yoru::speech::TranscriptionCompleted& event) {
            completed_ids.push_back(event.session_id);
        });

    if (skip_if_no_device(manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto first = manager.stop_session();
    REQUIRE(std::holds_alternative<QueuedJob>(first));

    // A is stuck in transcribe(); B must still be able to start and stop.
    CHECK(manager.state() == ServiceState::Processing);
    REQUIRE_FALSE(manager.start_session().has_value());
    CHECK(manager.state() == ServiceState::Recording);
    CHECK(manager.transcription_busy());
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto second = manager.stop_session();
    REQUIRE(std::holds_alternative<QueuedJob>(second));
    CHECK(std::get<QueuedJob>(second).position >= 1);

    speech_backend.open_gate();
    REQUIRE(pump_until(manager, 2));

    // FIFO: results come out in the order the recordings were stopped.
    REQUIRE(completed_ids.size() == 2);
    CHECK(completed_ids[0] == std::get<QueuedJob>(first).session_id);
    CHECK(completed_ids[1] == std::get<QueuedJob>(second).session_id);
}

TEST_CASE("start_session() is rejected, and reported, while the transcription queue is full") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    speech_backend.close_gate();
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{}, 1);

    std::optional<ErrorOccurred> error_event;
    bus.subscribe<ErrorOccurred>([&](const ErrorOccurred& event) { error_event = event; });

    // Capacity 1 and a stuck worker: job A is taken by the worker, B fills
    // the single slot, so a third recording cannot be accepted.
    for (int i = 0; i < 2; ++i) {
        if (skip_if_no_device(manager.start_session())) {
            speech_backend.open_gate();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        REQUIRE(std::holds_alternative<QueuedJob>(manager.stop_session()));
        if (i == 0) {
            // Wait until the worker has actually taken job A off the queue.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (speech_backend.calls().empty() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }
        }
    }

    const auto rejected = manager.start_session();
    REQUIRE(rejected.has_value());
    CHECK(rejected->message == "transcription queue is full");
    CHECK(manager.state() == ServiceState::Processing);
    REQUIRE(error_event.has_value());
    CHECK(error_event->component == "queue");

    speech_backend.open_gate();
    REQUIRE(pump_until(manager, 2));
    CHECK_FALSE(manager.start_session().has_value());
    manager.cancel_session();
}

TEST_CASE("cancel_job() removes a queued job but not the one in progress") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    speech_backend.close_gate();
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    std::vector<SessionId> cancelled;
    bus.subscribe<SessionCancelled>(
        [&](const SessionCancelled& event) { cancelled.push_back(event.session_id); });

    std::vector<SessionId> ids;
    for (int i = 0; i < 2; ++i) {
        if (skip_if_no_device(manager.start_session())) {
            speech_backend.open_gate();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        const auto result = manager.stop_session();
        REQUIRE(std::holds_alternative<QueuedJob>(result));
        ids.push_back(std::get<QueuedJob>(result).session_id);
        if (i == 0) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (speech_backend.calls().empty() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }
        }
    }

    CHECK_FALSE(manager.cancel_job(ids[0])); // running: cannot be interrupted
    CHECK(manager.cancel_job(ids[1]));       // still queued
    REQUIRE(cancelled.size() == 1);
    CHECK(cancelled[0] == ids[1]);

    speech_backend.open_gate();
    REQUIRE(pump_until(manager, 1));
    CHECK(speech_backend.calls().size() == 1);
}

TEST_CASE("shutdown() aborts the job in progress and drops the queued ones") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    speech_backend.close_gate();
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    for (int i = 0; i < 2; ++i) {
        if (skip_if_no_device(manager.start_session())) {
            speech_backend.open_gate();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        REQUIRE(std::holds_alternative<QueuedJob>(manager.stop_session()));
        if (i == 0) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (speech_backend.calls().empty() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }
        }
    }

    CHECK(manager.shutdown() == 1);
    CHECK_FALSE(manager.transcription_busy());
    CHECK(manager.state() == ServiceState::Idle);
}

TEST_CASE("stop_session() passes the configured language and prompt to the Speech Backend") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    Configuration configuration;
    configuration.default_language = "pt";
    configuration.transcription_prompt = "mixing English terms into Portuguese speech";
    SessionManager manager(bus, recording_manager, speech_backend, configuration);

    if (skip_if_no_device(manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    manager.stop_session();
    REQUIRE(pump_until(manager, 1));

    const auto request = speech_backend.request_seen();
    REQUIRE(request.has_value());
    CHECK(request->language == "pt");
    CHECK(request->initial_prompt == "mixing English terms into Portuguese speech");
}

TEST_CASE("a ConfigurationChanged after construction updates the language used on stop_session()") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    Configuration updated;
    updated.default_language = "es";
    bus.publish(yoru::config::ConfigurationChanged{.configuration = updated});

    if (skip_if_no_device(manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    manager.stop_session();
    REQUIRE(pump_until(manager, 1));

    const auto request = speech_backend.request_seen();
    REQUIRE(request.has_value());
    CHECK(request->language == "es");
}

TEST_CASE("a transcription failure is reported for that job and leaves the service Idle") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    FakeSpeechBackend speech_backend;
    speech_backend.should_fail = true;
    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    std::optional<ErrorOccurred> error_event;
    bus.subscribe<ErrorOccurred>([&](const ErrorOccurred& event) { error_event = event; });

    if (skip_if_no_device(manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    const auto stop_result = manager.stop_session();
    REQUIRE(std::holds_alternative<QueuedJob>(stop_result));
    REQUIRE(pump_until(manager, 1));

    // Unlike a capture failure, this concerns one job: no Error state to
    // recover from, and the next session can start right away.
    CHECK(manager.state() == ServiceState::Idle);
    REQUIRE(error_event.has_value());
    CHECK(error_event->component == "speech");
    CHECK(error_event->session_id == std::get<QueuedJob>(stop_result).session_id);
    CHECK(manager.status().last_failed == std::get<QueuedJob>(stop_result).session_id);

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

    SessionManager manager(bus, recording_manager, speech_backend, Configuration{});

    if (skip_if_no_device(manager.start_session())) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    std::optional<Transcript> transcript;
    bus.subscribe<yoru::speech::TranscriptionCompleted>(
        [&](const yoru::speech::TranscriptionCompleted& event) { transcript = event.transcript; });

    const auto result = manager.stop_session();

    REQUIRE(std::holds_alternative<QueuedJob>(result));
    REQUIRE(pump_until(manager, 1));
    CHECK(manager.state() == ServiceState::Idle);
    REQUIRE(transcript.has_value());
    CHECK(transcript->audio_duration > std::chrono::milliseconds{0});
}
