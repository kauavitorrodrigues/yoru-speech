#include "core/event_bus.hpp"
#include "domains/session/events.hpp"

#include <doctest/doctest.h>

using yoru::core::EventBus;
using yoru::session::ErrorOccurred;
using yoru::session::RecordingFinished;
using yoru::session::RecordingStarted;
using yoru::session::SessionId;

TEST_CASE("RecordingStarted and RecordingFinished carry the session id") {
    EventBus bus;
    SessionId started_id{};
    SessionId finished_id{};

    bus.subscribe<RecordingStarted>(
        [&](const RecordingStarted& event) { started_id = event.session_id; });
    bus.subscribe<RecordingFinished>(
        [&](const RecordingFinished& event) { finished_id = event.session_id; });

    bus.publish(RecordingStarted{SessionId{7}});
    bus.publish(RecordingFinished{SessionId{7}});

    CHECK(started_id == SessionId{7});
    CHECK(finished_id == SessionId{7});
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

TEST_CASE("ErrorOccurred carries the session id when tied to a session") {
    EventBus bus;
    std::optional<SessionId> received;

    bus.subscribe<ErrorOccurred>([&](const ErrorOccurred& event) { received = event.session_id; });

    bus.publish(ErrorOccurred{
        .session_id = SessionId{3},
        .component = "audio",
        .message = "microphone disconnected",
    });

    REQUIRE(received.has_value());
    CHECK(received.value() == SessionId{3});
}
