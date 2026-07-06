#pragma once

#include "core/session_id.hpp"

namespace yoru::session {

// Domain event published by the Session Manager. Represents a fact that
// already happened, never something expected to happen.
//
// There is no SessionStarted/SessionStopped counterpart: RecordingStarted
// (domains/audio) and TranscriptionCompleted (domains/speech), already
// published as the Session Manager delegates to those domains, serve that
// role. Cancellation has no such stand-in, since neither the Recording
// Manager nor the Speech Engine knows a session was cancelled rather than
// simply stopped, hence this dedicated event.
struct SessionCancelled {
    core::SessionId session_id;
};

} // namespace yoru::session
