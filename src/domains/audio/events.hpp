#pragma once

#include "domains/audio/recording.hpp"
#include "domains/session/session.hpp"

namespace yoru::audio {

// Domain events published by the Recording Manager. Each represents a
// fact that already happened, never something expected to happen.
// Recording is scoped to a session, so both events carry a SessionId,
// following the domain model's convention for session-scoped events.
//
// These live here, not in domains/session, because the domain that
// publishes an event owns its definition: the Recording Manager, which
// lives in this domain, is what publishes them.

struct RecordingStarted {
    session::SessionId session_id;
};

struct RecordingFinished {
    session::SessionId session_id;
    Recording recording;
};

} // namespace yoru::audio
