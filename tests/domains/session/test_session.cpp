#include "domains/session/session.hpp"

#include <doctest/doctest.h>

using yoru::session::can_transition;
using yoru::session::Session;
using yoru::session::SessionId;
using yoru::session::SessionState;

namespace {

std::chrono::system_clock::time_point fixed_time() {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
}

} // namespace

TEST_CASE("session starts in Created state with no finish time") {
    const Session session(SessionId{1}, fixed_time());

    CHECK(session.state() == SessionState::Created);
    CHECK(session.created_at() == fixed_time());
    CHECK_FALSE(session.finished_at().has_value());
}

TEST_CASE("session progresses through the happy path") {
    Session session(SessionId{1}, fixed_time());

    CHECK(session.try_transition(SessionState::Recording, fixed_time()));
    CHECK(session.state() == SessionState::Recording);
    CHECK_FALSE(session.finished_at().has_value());

    CHECK(session.try_transition(SessionState::Processing, fixed_time()));
    CHECK(session.state() == SessionState::Processing);
    CHECK_FALSE(session.finished_at().has_value());

    const auto finish_time = fixed_time() + std::chrono::seconds{5};
    CHECK(session.try_transition(SessionState::Completed, finish_time));
    CHECK(session.state() == SessionState::Completed);
    REQUIRE(session.finished_at().has_value());
    CHECK(session.finished_at().value() == finish_time);
}

TEST_CASE("failure is reachable from Recording and Processing") {
    Session recording_failure(SessionId{1}, fixed_time());
    REQUIRE(recording_failure.try_transition(SessionState::Recording, fixed_time()));
    CHECK(recording_failure.try_transition(SessionState::Failed, fixed_time()));
    CHECK(recording_failure.state() == SessionState::Failed);
    CHECK(recording_failure.finished_at().has_value());

    Session processing_failure(SessionId{2}, fixed_time());
    REQUIRE(processing_failure.try_transition(SessionState::Recording, fixed_time()));
    REQUIRE(processing_failure.try_transition(SessionState::Processing, fixed_time()));
    CHECK(processing_failure.try_transition(SessionState::Failed, fixed_time()));
    CHECK(processing_failure.state() == SessionState::Failed);
}

TEST_CASE("invalid transitions are rejected and leave the session unchanged") {
    Session session(SessionId{1}, fixed_time());

    CHECK_FALSE(session.try_transition(SessionState::Processing, fixed_time()));
    CHECK_FALSE(session.try_transition(SessionState::Completed, fixed_time()));
    CHECK_FALSE(session.try_transition(SessionState::Failed, fixed_time()));
    CHECK(session.state() == SessionState::Created);
    CHECK_FALSE(session.finished_at().has_value());
}

TEST_CASE("terminal states reject further transitions") {
    Session session(SessionId{1}, fixed_time());
    REQUIRE(session.try_transition(SessionState::Recording, fixed_time()));
    REQUIRE(session.try_transition(SessionState::Processing, fixed_time()));
    REQUIRE(session.try_transition(SessionState::Completed, fixed_time()));

    CHECK_FALSE(session.try_transition(SessionState::Recording, fixed_time()));
    CHECK_FALSE(session.try_transition(SessionState::Created, fixed_time()));
    CHECK_FALSE(session.try_transition(SessionState::Failed, fixed_time()));
    CHECK(session.state() == SessionState::Completed);
}

TEST_CASE("transitioning to the same state is always rejected") {
    constexpr SessionState states[] = {
        SessionState::Created,   SessionState::Recording, SessionState::Processing,
        SessionState::Completed, SessionState::Failed,
    };

    for (const auto state : states) {
        CHECK_FALSE(can_transition(state, state));
    }
}
