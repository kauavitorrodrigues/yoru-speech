#include "domains/audio/recording_manager.hpp"

#include "core/event_bus.hpp"
#include "domains/audio/events.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <thread>
#include <variant>

using yoru::audio::Recording;
using yoru::audio::RecordingError;
using yoru::audio::RecordingFinished;
using yoru::audio::RecordingManager;
using yoru::audio::RecordingStarted;
using yoru::core::EventBus;
using yoru::core::SessionId;

// Tests below that call start() need a real capture device. On a host
// without one (e.g. headless CI), start() reports it through the normal
// error channel rather than crashing; skip out gracefully rather than
// failing on an environment limitation the test isn't meant to catch.

TEST_CASE("start() twice reports that a recording is already in progress") {
    EventBus bus;
    RecordingManager manager(bus);

    const auto first_error = manager.start(SessionId{1});
    if (first_error.has_value()) {
        MESSAGE("skipping: no capture device available (", first_error->message, ")");
        return;
    }

    const auto second_error = manager.start(SessionId{2});

    REQUIRE(second_error.has_value());
    CHECK(manager.is_recording());

    manager.stop();
}

TEST_CASE("stop() without a recording in progress reports an error and does not crash") {
    EventBus bus;
    RecordingManager manager(bus);

    const auto result = manager.stop();

    REQUIRE(std::holds_alternative<RecordingError>(result));
    CHECK_FALSE(manager.is_recording());
}

TEST_CASE("start() then stop() captures real audio and publishes both events") {
    EventBus bus;
    RecordingManager manager(bus);

    std::optional<SessionId> started_id;
    std::optional<RecordingFinished> finished;
    bus.subscribe<RecordingStarted>(
        [&](const RecordingStarted& event) { started_id = event.session_id; });
    bus.subscribe<RecordingFinished>([&](const RecordingFinished& event) { finished = event; });

    const auto start_error = manager.start(SessionId{42});
    if (start_error.has_value()) {
        MESSAGE("skipping: no capture device available (", start_error->message, ")");
        return;
    }
    CHECK(manager.is_recording());

    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    const auto stop_result = manager.stop();
    CHECK_FALSE(manager.is_recording());

    REQUIRE(std::holds_alternative<Recording>(stop_result));
    const auto& returned_recording = std::get<Recording>(stop_result);
    CHECK_FALSE(returned_recording.samples.empty());
    CHECK(returned_recording.duration() > std::chrono::milliseconds{0});

    REQUIRE(started_id.has_value());
    CHECK(started_id.value() == SessionId{42});

    REQUIRE(finished.has_value());
    CHECK(finished->session_id == SessionId{42});
    CHECK_FALSE(finished->recording.samples.empty());
    CHECK(finished->recording.duration() > std::chrono::milliseconds{0});
}
