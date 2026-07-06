#include "core/event_bus.hpp"
#include "domains/audio/events.hpp"

#include <doctest/doctest.h>

#include <cstddef>

using yoru::audio::Recording;
using yoru::audio::RecordingFinished;
using yoru::audio::RecordingStarted;
using yoru::core::EventBus;
using yoru::session::SessionId;

TEST_CASE("RecordingStarted carries the session id") {
    EventBus bus;
    SessionId received{};

    bus.subscribe<RecordingStarted>(
        [&](const RecordingStarted& event) { received = event.session_id; });

    bus.publish(RecordingStarted{SessionId{7}});

    CHECK(received == SessionId{7});
}

TEST_CASE("RecordingFinished carries the session id and the Recording") {
    EventBus bus;
    SessionId received_id{};
    std::size_t received_sample_count = 0;

    bus.subscribe<RecordingFinished>([&](const RecordingFinished& event) {
        received_id = event.session_id;
        received_sample_count = event.recording.samples.size();
    });

    bus.publish(RecordingFinished{
        .session_id = SessionId{7},
        .recording =
            Recording{
                .samples = {0.1F, 0.2F, 0.3F},
                .sample_rate = 16000,
                .channels = 1,
            },
    });

    CHECK(received_id == SessionId{7});
    CHECK(received_sample_count == 3);
}
