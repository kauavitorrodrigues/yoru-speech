#pragma once

#include "core/session_id.hpp"
#include "domains/session/service_state.hpp"

#include <cstddef>
#include <optional>

namespace yoru::session {

// A recording accepted by SessionManager::stop_session() and waiting for
// (or undergoing) transcription. The Transcript arrives later, via
// TranscriptionCompleted.
struct QueuedJob {
    core::SessionId session_id{};
    // Jobs ahead of this one, including the one being transcribed.
    std::size_t position = 0;
};

// A snapshot of capture and transcription activity, for status replies.
// Plain values only, so serialization layers can depend on it without
// pulling in the Session Manager.
struct SessionStatus {
    ServiceState state = ServiceState::Idle;
    std::size_t queued = 0;  // jobs waiting, not counting the one in progress
    bool processing = false; // a job is being transcribed right now
    std::optional<core::SessionId> last_completed;
    std::optional<core::SessionId> last_failed;
};

} // namespace yoru::session
