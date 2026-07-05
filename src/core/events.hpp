#pragma once

#include "domains/config/configuration.hpp"
#include "domains/session/session.hpp"
#include "domains/speech/model.hpp"
#include "domains/speech/transcript.hpp"

#include <optional>
#include <string>

namespace yoru::core {

// Domain events distributed through the EventBus. Each event represents a
// fact that already happened — never something expected to happen.

struct RecordingStarted {
    yoru::session::SessionId session_id;
};

struct RecordingFinished {
    yoru::session::SessionId session_id;
};

struct TranscriptionStarted {
    yoru::session::SessionId session_id;
};

struct TranscriptionCompleted {
    yoru::session::SessionId session_id;
    yoru::speech::Transcript transcript;
};

struct ErrorOccurred {
    // Absent when the error is not tied to a specific session (e.g. a
    // configuration load failure at startup).
    std::optional<yoru::session::SessionId> session_id;
    std::string component;
    std::string message;
};

// Models are reusable resources, never scoped to a session (domain model).
struct ModelLoaded {
    yoru::speech::Model model;
};

// Configuration is global to the service instance, never session-scoped.
struct ConfigurationChanged {
    yoru::config::Configuration configuration;
};

} // namespace yoru::core
