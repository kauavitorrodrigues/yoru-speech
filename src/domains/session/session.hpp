#pragma once

#include "core/session_id.hpp"

#include <chrono>
#include <optional>

namespace yoru::session {

// Lifecycle states of a single session. Distinct from ServiceState: this
// tracks the progress of one unit of work, not the service as a whole.
enum class SessionState {
    Created,
    Recording,
    Processing,
    Completed,
    Failed,
};

// Validates whether a transition between two session states is allowed.
//
// Allowed transitions:
//
//     Created ──► Recording ──► Processing ──► Completed
//                     │               │
//                     └────► Failed ◄─┘
//
// Completed and Failed are terminal: no transition leaves them.
bool can_transition(SessionState from, SessionState to);

// Represents one complete dictation cycle: the unit of work of the system.
// A session never shares a recording with another session.
class Session {
public:
    Session(core::SessionId id, std::chrono::system_clock::time_point created_at);

    core::SessionId id() const;
    SessionState state() const;
    std::chrono::system_clock::time_point created_at() const;
    std::optional<std::chrono::system_clock::time_point> finished_at() const;

    // Attempts to move the session to `next`. Returns false and leaves the
    // session unchanged if the transition is not allowed. Recording
    // `finished_at` when entering a terminal state (Completed or Failed).
    bool try_transition(SessionState next, std::chrono::system_clock::time_point when);

private:
    core::SessionId id_;
    SessionState state_ = SessionState::Created;
    std::chrono::system_clock::time_point created_at_;
    std::optional<std::chrono::system_clock::time_point> finished_at_;
};

} // namespace yoru::session
