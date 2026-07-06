#include "core/event_bus.hpp"
#include "domains/speech/events.hpp"

#include <doctest/doctest.h>

using yoru::core::EventBus;
using yoru::core::SessionId;
using yoru::speech::Model;
using yoru::speech::ModelLoaded;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionCompleted;
using yoru::speech::TranscriptionStarted;

TEST_CASE("TranscriptionStarted carries the session id") {
    EventBus bus;
    SessionId received{};

    bus.subscribe<TranscriptionStarted>(
        [&](const TranscriptionStarted& event) { received = event.session_id; });

    bus.publish(TranscriptionStarted{SessionId{5}});

    CHECK(received == SessionId{5});
}

TEST_CASE("TranscriptionCompleted carries the session id and the Transcript") {
    EventBus bus;
    SessionId received_id{};
    std::string received_text;

    bus.subscribe<TranscriptionCompleted>([&](const TranscriptionCompleted& event) {
        received_id = event.session_id;
        received_text = event.transcript.text;
    });

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

    CHECK(received_id == SessionId{1});
    CHECK(received_text == "hello");
}

TEST_CASE("ModelLoaded is not session-scoped") {
    EventBus bus;
    bool received = false;

    bus.subscribe<ModelLoaded>([&](const ModelLoaded&) { received = true; });

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

    CHECK(received);
}
