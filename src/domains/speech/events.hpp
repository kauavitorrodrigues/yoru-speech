#pragma once

#include "core/session_id.hpp"
#include "domains/speech/model.hpp"
#include "domains/speech/transcript.hpp"

namespace yoru::speech {

// Domain events published by the Speech Engine. Each represents a fact that
// already happened, never something expected to happen.

// The transcription worker began decoding this session's recording. It may
// be published a little after the fact (the worker cannot publish itself;
// the Session Manager reports it from its own thread), but always before
// the matching TranscriptionCompleted or failure.
struct TranscriptionStarted {
    core::SessionId session_id;
};

struct TranscriptionCompleted {
    core::SessionId session_id;
    Transcript transcript;
};

// Models are reusable resources, never scoped to a session (domain model).
struct ModelLoaded {
    Model model;
};

} // namespace yoru::speech
