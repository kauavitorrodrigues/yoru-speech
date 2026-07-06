#include "domains/ipc/ipc_service.hpp"

#include "domains/audio/recording_manager.hpp"
#include "domains/config/configuration_manager.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/speech_backend.hpp"
#include "test_client.hpp"

#include <unistd.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using yoru::audio::RecordingManager;
using yoru::config::ConfigurationManager;
using yoru::core::EventBus;
using yoru::ipc::IpcService;
using yoru::ipc::test::TestClient;
using yoru::ipc::test::unique_test_socket_path;
using yoru::session::SessionManager;
using yoru::speech::Model;
using yoru::speech::SpeechBackend;
using yoru::speech::SpeechError;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionRequest;
using yoru::speech::TranscriptionResult;

namespace {

// Same role as CommandDispatcher's own tests: a SpeechBackend that never
// touches whisper.cpp, so this test doesn't need a real model.
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
};

std::filesystem::path unique_test_config_path() {
    return std::filesystem::temp_directory_path() /
           ("yoru-speech-test-ipc-service-config-" + std::to_string(getpid()) + ".toml");
}

// Bundles every collaborator IpcService needs, plus the service itself,
// already started.
struct Fixture {
    std::filesystem::path config_path = unique_test_config_path();
    std::filesystem::path socket_path = unique_test_socket_path("yoru-speech-test-ipc-service");
    EventBus bus;
    RecordingManager recording_manager{bus};
    FakeSpeechBackend speech_backend;
    ConfigurationManager configuration_manager{config_path, bus};
    SessionManager session_manager{bus, recording_manager, speech_backend};
    IpcService service{bus, session_manager, configuration_manager, socket_path,
                       std::filesystem::temp_directory_path() /
                           "yoru-speech-test-ipc-empty-models"};

    Fixture() {
        std::filesystem::remove(config_path);
        std::filesystem::remove(socket_path);
        configuration_manager.load();
        REQUIRE_FALSE(service.start().has_value());
    }

    ~Fixture() {
        service.stop();
        std::filesystem::remove(config_path);
    }

    void pump(int rounds = 20, int timeout_ms = 20) {
        for (int i = 0; i < rounds; ++i) {
            service.poll_once(timeout_ms);
        }
    }
};

} // namespace

TEST_CASE("a regular command is answered through the full IpcService pipeline") {
    Fixture fixture;
    TestClient client(fixture.socket_path);

    client.send_line(R"({"type":"get_state"})");
    fixture.pump();

    const auto response = client.read_line();
    CHECK(response.find(R"("state":"idle")") != std::string::npos);
}

TEST_CASE("a malformed message gets a clear error and the connection stays open") {
    Fixture fixture;
    TestClient client(fixture.socket_path);

    client.send_line("not json");
    fixture.pump();
    const auto first_response = client.read_line();
    CHECK(first_response.find(R"("ok":false)") != std::string::npos);

    // The connection is still usable after the malformed message.
    client.send_line(R"({"type":"get_state"})");
    fixture.pump();
    const auto second_response = client.read_line();
    CHECK(second_response.find(R"("ok":true)") != std::string::npos);
}

TEST_CASE("stop_recording without an active session reports a clear error") {
    Fixture fixture;
    TestClient client(fixture.socket_path);

    client.send_line(R"({"type":"stop_recording"})");
    fixture.pump();

    const auto response = client.read_line();
    CHECK(response.find(R"("ok":false)") != std::string::npos);
}

TEST_CASE("subscribe_events opts a client in to receiving pushed domain events") {
    Fixture fixture;
    TestClient client(fixture.socket_path);

    client.send_line(R"({"type":"subscribe_events"})");
    fixture.pump();
    const auto ack = client.read_line();
    CHECK(ack.find(R"("type":"subscribe_events")") != std::string::npos);
    CHECK(ack.find(R"("ok":true)") != std::string::npos);

    // set_config publishes ConfigurationChanged from inside
    // ConfigurationManager::update(), which runs to completion (and thus
    // pushes the event) before CommandDispatcher::handle_line() returns
    // its own response. So the pushed event reaches the client before
    // the command's own response does, not after.
    client.send_line(R"({"type":"set_config","default_language":"pt"})");
    fixture.pump();

    const auto pushed_event = client.read_line();
    CHECK(pushed_event.find(R"("event":"configuration_changed")") != std::string::npos);
    CHECK(pushed_event.find(R"("default_language":"pt")") != std::string::npos);

    const auto command_response = client.read_line();
    CHECK(command_response.find(R"("type":"set_config")") != std::string::npos);
}

TEST_CASE("unsubscribe_events stops further event pushes") {
    Fixture fixture;
    TestClient client(fixture.socket_path);

    client.send_line(R"({"type":"subscribe_events"})");
    fixture.pump();
    client.read_line(); // the subscribe_events ack

    client.send_line(R"({"type":"unsubscribe_events"})");
    fixture.pump();
    const auto unsubscribe_ack = client.read_line();
    CHECK(unsubscribe_ack.find(R"("ok":true)") != std::string::npos);

    client.send_line(R"({"type":"set_config","default_language":"pt"})");
    fixture.pump();
    const auto command_response = client.read_line();
    CHECK(command_response.find(R"("type":"set_config")") != std::string::npos);

    // No event follows: only the command response was in the pipe, so a
    // further read_line() finds nothing within its short budget.
    const auto nothing = client.read_line(5);
    CHECK(nothing.empty());
}

TEST_CASE("a disconnected client's event subscription is forgotten without crashing") {
    Fixture fixture;
    {
        TestClient client(fixture.socket_path);
        client.send_line(R"({"type":"subscribe_events"})");
        fixture.pump();
        client.read_line();
        client.close_connection();
    }
    fixture.pump();

    // The event push for the disconnected subscriber (if it were still
    // wrongly tracked) would have nowhere to go; a fresh client sending
    // an ordinary command proves the service is still fully functional,
    // not just "didn't crash yet".
    TestClient new_client(fixture.socket_path);
    new_client.send_line(R"({"type":"set_config","default_language":"pt"})");
    fixture.pump();

    const auto response = new_client.read_line();
    CHECK(response.find(R"("ok":true)") != std::string::npos);
}
