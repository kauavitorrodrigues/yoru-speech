#include "session.hpp"

namespace yoru::session {

bool can_transition(SessionState from, SessionState to) {
    switch (from) {
    case SessionState::Created:
        return to == SessionState::Recording;
    case SessionState::Recording:
        return to == SessionState::Processing || to == SessionState::Failed;
    case SessionState::Processing:
        return to == SessionState::Completed || to == SessionState::Failed;
    case SessionState::Completed:
    case SessionState::Failed:
        return false;
    }
    return false;
}

Session::Session(core::SessionId id, std::chrono::system_clock::time_point created_at)
    : id_(id), created_at_(created_at) {}

core::SessionId Session::id() const {
    return id_;
}

SessionState Session::state() const {
    return state_;
}

std::chrono::system_clock::time_point Session::created_at() const {
    return created_at_;
}

std::optional<std::chrono::system_clock::time_point> Session::finished_at() const {
    return finished_at_;
}

bool Session::try_transition(SessionState next, std::chrono::system_clock::time_point when) {
    if (!can_transition(state_, next)) {
        return false;
    }

    state_ = next;
    if (next == SessionState::Completed || next == SessionState::Failed) {
        finished_at_ = when;
    }
    return true;
}

} // namespace yoru::session
