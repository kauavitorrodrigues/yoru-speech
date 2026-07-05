#include "core/event_bus.hpp"
#include "core/events.hpp"

#include <doctest/doctest.h>

using yoru::core::ConfigurationChanged;
using yoru::core::ErrorOccurred;
using yoru::core::EventBus;
using yoru::core::ModelLoaded;
using yoru::core::RecordingFinished;
using yoru::core::RecordingStarted;
using yoru::core::TranscriptionCompleted;
using yoru::core::TranscriptionStarted;
using yoru::session::SessionId;
using yoru::speech::Model;
using yoru::speech::Transcript;

TEST_CASE("session lifecycle events carry the originating session id") {
    EventBus bus;
    SessionId received{};

    bus.subscribe<RecordingStarted>(
        [&](const RecordingStarted& event) { received = event.session_id; });

    bus.publish(RecordingStarted{SessionId{7}});

    CHECK(received == SessionId{7});
}

TEST_CASE("TranscriptionCompleted carries the produced Transcript") {
    EventBus bus;
    std::string received_text;

    bus.subscribe<TranscriptionCompleted>(
        [&](const TranscriptionCompleted& event) { received_text = event.transcript.text; });

    bus.publish(TranscriptionCompleted{
        .session_id = SessionId{1},
        .transcript =
            Transcript{
                .text = "hello",
                .detected_language = "en",
                .requested_language = "en",
                .audio_duration = std::chrono::milliseconds{1000},
                .processing_time = std::chrono::milliseconds{50},
            },
    });

    CHECK(received_text == "hello");
}

TEST_CASE("ErrorOccurred allows an absent session id for non-session errors") {
    EventBus bus;
    bool received_without_session = false;

    bus.subscribe<ErrorOccurred>([&](const ErrorOccurred& event) {
        received_without_session = !event.session_id.has_value();
    });

    bus.publish(ErrorOccurred{
        .session_id = std::nullopt,
        .component = "config",
        .message = "failed to load config.toml",
    });

    CHECK(received_without_session);
}

TEST_CASE("ModelLoaded and ConfigurationChanged are not session-scoped") {
    EventBus bus;
    bool model_received = false;
    bool config_received = false;

    bus.subscribe<ModelLoaded>([&](const ModelLoaded&) { model_received = true; });
    bus.subscribe<ConfigurationChanged>(
        [&](const ConfigurationChanged&) { config_received = true; });

    bus.publish(ModelLoaded{
        .model =
            Model{
                .name = "ggml-base",
                .size = yoru::speech::ModelSize::Base,
                .supported_language = "multi",
                .path = "/models/ggml-base.bin",
                .backend = "whisper.cpp",
            },
    });
    bus.publish(ConfigurationChanged{});

    CHECK(model_received);
    CHECK(config_received);
}

TEST_CASE("marker events with no extra data still carry their session id") {
    EventBus bus;
    int calls = 0;

    bus.subscribe<RecordingFinished>([&](const RecordingFinished&) { ++calls; });
    bus.subscribe<TranscriptionStarted>([&](const TranscriptionStarted&) { ++calls; });

    bus.publish(RecordingFinished{SessionId{3}});
    bus.publish(TranscriptionStarted{SessionId{3}});

    CHECK(calls == 2);
}
