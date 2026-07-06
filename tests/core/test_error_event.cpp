#include "core/error_event.hpp"

#include "core/event_bus.hpp"

#include <doctest/doctest.h>

using yoru::core::ErrorOccurred;
using yoru::core::EventBus;
using yoru::core::SessionId;

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
