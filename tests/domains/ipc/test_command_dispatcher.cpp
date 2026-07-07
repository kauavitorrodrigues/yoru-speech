#include "domains/ipc/command_dispatcher.hpp"

#include "domains/audio/recording_manager.hpp"
#include "domains/config/configuration_manager.hpp"
#include "domains/ipc/message_codec.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/speech_backend.hpp"

#include <unistd.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using yoru::audio::RecordingManager;
using yoru::config::Configuration;
using yoru::config::ConfigurationManager;
using yoru::core::EventBus;
using yoru::ipc::CommandDispatcher;
using yoru::ipc::parse_message;
using yoru::session::SessionManager;
using yoru::speech::Model;
using yoru::speech::SpeechBackend;
using yoru::speech::SpeechError;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionRequest;
using yoru::speech::TranscriptionResult;

namespace {

// Same role as RecordingManager's own tests and SessionManager's tests:
// a SpeechBackend that never touches whisper.cpp, so the dispatcher's
// command-translation logic can be tested deterministically.
class FakeSpeechBackend : public SpeechBackend {
public:
    std::optional<SpeechError> load_model(const Model&) override {
        return std::nullopt;
    }

    void unload_model() override {}

    bool has_model_loaded() const override {
        return true;
    }

    TranscriptionResult transcribe(yoru::core::SessionId /*session_id*/,
                                   const std::vector<float>& samples,
                                   const TranscriptionRequest& request) override {
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
        return transcribe(yoru::core::SessionId{0}, window, request);
    }
};

std::filesystem::path unique_test_config_path() {
    return std::filesystem::temp_directory_path() /
           ("yoru-speech-test-config-" + std::to_string(getpid()) + ".toml");
}

// A fixture bundling everything a CommandDispatcher needs, so each test
// only has to declare one of these instead of five collaborators.
struct Fixture {
    std::filesystem::path config_path = unique_test_config_path();
    EventBus bus;
    RecordingManager recording_manager{bus};
    FakeSpeechBackend speech_backend;
    ConfigurationManager configuration_manager{config_path, bus};
    SessionManager session_manager{bus, recording_manager, speech_backend,
                                   configuration_manager.current()};
    CommandDispatcher dispatcher{session_manager, configuration_manager,
                                 std::filesystem::temp_directory_path() /
                                     "yoru-speech-test-empty-models-dir"};

    Fixture() {
        std::filesystem::remove(config_path);
        configuration_manager.load();
    }

    ~Fixture() {
        std::filesystem::remove(config_path);
    }
};

// A fresh Fixture's first start_recording can only fail because no
// capture device is available (there is no active session yet to
// collide with), so any failure response here is that case. Skip
// gracefully rather than failing, as established for RecordingManager's
// and SessionManager's own device-dependent tests.
bool skip_if_no_device(const std::string& start_recording_response) {
    if (start_recording_response.find(R"("ok":false)") == std::string::npos) {
        return false;
    }
    MESSAGE("skipping: no capture device available (", start_recording_response, ")");
    return true;
}

} // namespace

TEST_CASE("handle_line() reports an error for malformed JSON without crashing") {
    Fixture fixture;

    const auto response = fixture.dispatcher.handle_line("not json");

    CHECK(response.find(R"("ok":false)") != std::string::npos);
}

TEST_CASE("handle_line() reports an error for an unrecognized command") {
    Fixture fixture;

    const auto response = fixture.dispatcher.handle_line(R"({"type":"do_a_backflip"})");

    const auto parsed = parse_message(response);
    REQUIRE(parsed.type.has_value());
    CHECK(response.find(R"("ok":false)") != std::string::npos);
    CHECK(response.find("do_a_backflip") != std::string::npos);
}

TEST_CASE("get_state reports Idle before any session starts") {
    Fixture fixture;

    const auto response = fixture.dispatcher.handle_line(R"({"type":"get_state"})");

    CHECK(response.find(R"("state":"idle")") != std::string::npos);
}

TEST_CASE("get_config reports the current configuration") {
    Fixture fixture;

    const auto response = fixture.dispatcher.handle_line(R"({"type":"get_config"})");

    CHECK(response.find(R"("ok":true)") != std::string::npos);
    CHECK(response.find("default_language") != std::string::npos);
}

TEST_CASE("set_config applies a valid configuration and returns it") {
    Fixture fixture;

    const auto response = fixture.dispatcher.handle_line(
        R"({"type":"set_config","default_language":"pt","auto_clipboard":false})");

    REQUIRE(response.find(R"("ok":true)") != std::string::npos);
    CHECK(response.find(R"("default_language":"pt")") != std::string::npos);
    CHECK(response.find(R"("auto_clipboard":false)") != std::string::npos);
    CHECK(fixture.configuration_manager.current().default_language == "pt");
}

TEST_CASE("set_config preserves fields not present in the request instead of resetting them") {
    Fixture fixture;
    REQUIRE(fixture.dispatcher.handle_line(R"({"type":"set_config","selected_model":"small"})")
                .find(R"("ok":true)") != std::string::npos);

    const auto response =
        fixture.dispatcher.handle_line(R"({"type":"set_config","auto_clipboard":false})");

    REQUIRE(response.find(R"("ok":true)") != std::string::npos);
    CHECK(fixture.configuration_manager.current().selected_model == "small");
    CHECK_FALSE(fixture.configuration_manager.current().auto_clipboard);
}

TEST_CASE("set_config rejects an invalid configuration and leaves the current one untouched") {
    Fixture fixture;
    const auto original_language = fixture.configuration_manager.current().default_language;

    const auto response =
        fixture.dispatcher.handle_line(R"({"type":"set_config","default_language":"not-a-code"})");

    CHECK(response.find(R"("ok":false)") != std::string::npos);
    CHECK(fixture.configuration_manager.current().default_language == original_language);
}

TEST_CASE("list_models on an empty directory returns an empty, successful list") {
    Fixture fixture;

    const auto response = fixture.dispatcher.handle_line(R"({"type":"list_models"})");

    CHECK(response.find(R"("ok":true)") != std::string::npos);
    CHECK(response.find(R"("models":[])") != std::string::npos);
}

TEST_CASE("cancel_session when idle reports an error") {
    Fixture fixture;

    const auto response = fixture.dispatcher.handle_line(R"({"type":"cancel_session"})");

    CHECK(response.find(R"("ok":false)") != std::string::npos);
}

TEST_CASE("start_recording, then stop_recording, returns a Transcript") {
    Fixture fixture;

    const auto start_response = fixture.dispatcher.handle_line(R"({"type":"start_recording"})");
    if (skip_if_no_device(start_response)) {
        return;
    }
    CHECK(start_response.find(R"("ok":true)") != std::string::npos);

    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    const auto stop_response = fixture.dispatcher.handle_line(R"({"type":"stop_recording"})");

    CHECK(stop_response.find(R"("ok":true)") != std::string::npos);
    CHECK(stop_response.find("fake transcript") != std::string::npos);
    CHECK(fixture.session_manager.state() == yoru::session::ServiceState::Idle);
}

TEST_CASE("start_recording twice reports that a session is already active") {
    Fixture fixture;

    const auto first_response = fixture.dispatcher.handle_line(R"({"type":"start_recording"})");
    if (skip_if_no_device(first_response)) {
        return;
    }

    const auto second_response = fixture.dispatcher.handle_line(R"({"type":"start_recording"})");

    CHECK(second_response.find(R"("ok":false)") != std::string::npos);

    fixture.dispatcher.handle_line(R"({"type":"cancel_session"})");
}
