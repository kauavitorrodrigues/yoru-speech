#pragma once

#include "core/session_id.hpp"
#include "domains/audio/recording.hpp"

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
    core::SessionId session_id;
};

struct RecordingFinished {
    core::SessionId session_id;
    Recording recording;
};

} // namespace yoru::audio
