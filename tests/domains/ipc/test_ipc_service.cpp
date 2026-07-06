#include "domains/ipc/ipc_service.hpp"

#include "domains/audio/capture_device.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/clipboard/auto_clipboard.hpp"
#include "domains/clipboard/wl_clipboard_adapter.hpp"
#include "domains/config/configuration_manager.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/events.hpp"
#include "domains/speech/speech_backend.hpp"
#include "test_client.hpp"

#include <unistd.h>

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using yoru::audio::RecordingManager;
using yoru::clipboard::AutoClipboard;
using yoru::clipboard::WlClipboardAdapter;
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
// touches whisper.cpp, so this test doesn't need a real model. Publishes
// TranscriptionStarted/TranscriptionCompleted like WhisperBackend does, so
// tests exercising downstream consumers of those events (e.g.
// AutoClipboard) see the same behavior a real backend would produce.
class FakeSpeechBackend : public SpeechBackend {
public:
    explicit FakeSpeechBackend(EventBus& event_bus) : event_bus_(event_bus) {}

    std::optional<SpeechError> load_model(const Model&) override {
        return std::nullopt;
    }

    void unload_model() override {}

    bool has_model_loaded() const override {
        return true;
    }

    TranscriptionResult transcribe(yoru::core::SessionId session_id,
                                   const std::vector<float>& samples,
                                   const TranscriptionRequest& request) override {
        event_bus_.publish(yoru::speech::TranscriptionStarted{session_id});
        Transcript transcript{
            .text = "fake transcript",
            .detected_language = "en",
            .requested_language = request.language,
            .audio_duration = std::chrono::milliseconds{static_cast<std::int64_t>(samples.size()) *
                                                        1000 / yoru::audio::kSampleRate},
            .processing_time = std::chrono::milliseconds{1},
        };
        event_bus_.publish(yoru::speech::TranscriptionCompleted{session_id, transcript});
        return transcript;
    }

private:
    EventBus& event_bus_;
};

std::filesystem::path unique_test_config_path() {
    return std::filesystem::temp_directory_path() /
           ("yoru-speech-test-ipc-service-config-" + std::to_string(getpid()) + ".toml");
}

// Bundles every collaborator IpcService needs, plus the service itself,
// already started. Also wires a real AutoClipboard (auto_clipboard
// defaults to true in Configuration{}), so a full dictation flow through
// this fixture exercises the same clipboard side effect production
// wiring does, not just the session/speech layers.
//
// auto_clipboard is declared (and thus destroyed) before session_manager
// and speech_backend, even though it only directly depends on bus and
// configuration_manager: EventBus::subscribe() has no unsubscribe, so a
// subscriber must never be destroyed while one of its potential
// publishers can still fire an event into it during teardown.
struct Fixture {
    std::filesystem::path config_path = unique_test_config_path();
    std::filesystem::path socket_path = unique_test_socket_path("yoru-speech-test-ipc-service");
    EventBus bus;
    RecordingManager recording_manager{bus};
    ConfigurationManager configuration_manager{config_path, bus};
    AutoClipboard auto_clipboard{bus, configuration_manager.current()};
    FakeSpeechBackend speech_backend{bus};
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

std::string read_clipboard() {
    FILE* pipe = ::popen("wl-paste -n 2>/dev/null", "r");
    REQUIRE(pipe != nullptr);

    std::string output;
    std::array<char, 256> chunk{};
    std::size_t bytes_read = 0;
    while ((bytes_read = std::fread(chunk.data(), 1, chunk.size(), pipe)) > 0) {
        output.append(chunk.data(), bytes_read);
    }
    ::pclose(pipe);
    return output;
}

// Real end-to-end dependencies (a capture device, a Wayland clipboard) may
// not exist in every environment this suite runs in (e.g. headless CI).
// Skip gracefully rather than failing on an environment limitation these
// tests aren't meant to catch, matching the pattern already established
// for RecordingManager's and WlClipboardAdapter's own real-dependency
// tests.
bool skip_if_no_clipboard() {
    const auto error = WlClipboardAdapter{}.copy("yoru-speech-ipc-test-clipboard-check");
    if (error.has_value()) {
        MESSAGE("skipping: no working clipboard available (", error->message, ")");
        return true;
    }
    return false;
}

// Starts recording, returning true (and skipping) if no capture device is
// available. Not a pure predicate: the caller relies on the recording
// having actually started when this returns false.
bool start_recording_or_skip(TestClient& client, Fixture& fixture) {
    client.send_line(R"({"type":"start_recording"})");
    fixture.pump();
    const auto response = client.read_line();
    if (response.find(R"("ok":true)") == std::string::npos) {
        MESSAGE("skipping: no capture device available (", response, ")");
        return true;
    }
    return false;
}

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

TEST_CASE("the full dictation flow works end-to-end: connect, start, stop, transcript, clipboard") {
    Fixture fixture;
    TestClient client(fixture.socket_path);

    // Checked before starting a recording, so a missing clipboard never
    // leaves this test having started one without stopping it.
    if (skip_if_no_clipboard()) {
        return;
    }
    if (start_recording_or_skip(client, fixture)) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    client.send_line(R"({"type":"stop_recording"})");
    fixture.pump();
    const auto response = client.read_line();

    CHECK(response.find(R"("ok":true)") != std::string::npos);
    CHECK(response.find(R"("text":"fake transcript")") != std::string::npos);
    CHECK(read_clipboard() == "fake transcript");
}

TEST_CASE("an error mid-flow does not corrupt state: a normal session still works right after") {
    Fixture fixture;
    TestClient client(fixture.socket_path);

    // stop_recording with nothing in progress is a client-error case,
    // rejected without touching service state (see
    // "stop_recording without an active session reports a clear error").
    client.send_line(R"({"type":"stop_recording"})");
    fixture.pump();
    const auto error_response = client.read_line();
    CHECK(error_response.find(R"("ok":false)") != std::string::npos);

    if (start_recording_or_skip(client, fixture)) {
        return;
    }

    client.send_line(R"({"type":"stop_recording"})");
    fixture.pump();
    const auto recovered_response = client.read_line();

    CHECK(recovered_response.find(R"("ok":true)") != std::string::npos);
    CHECK(recovered_response.find(R"("text":"fake transcript")") != std::string::npos);
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
