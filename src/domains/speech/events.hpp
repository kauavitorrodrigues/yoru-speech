#pragma once

#include "domains/session/session.hpp"
#include "domains/speech/model.hpp"
#include "domains/speech/transcript.hpp"

namespace yoru::speech {

// Domain events published by the Speech Engine. Each represents a fact that
// already happened, never something expected to happen.

struct TranscriptionStarted {
    yoru::session::SessionId session_id;
};

struct TranscriptionCompleted {
    yoru::session::SessionId session_id;
    Transcript transcript;
};

// Models are reusable resources, never scoped to a session (domain model).
struct ModelLoaded {
    Model model;
};

} // namespace yoru::speech
