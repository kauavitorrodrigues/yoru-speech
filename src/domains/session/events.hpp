#pragma once

#include "domains/session/session.hpp"

#include <optional>
#include <string>

namespace yoru::session {

// Domain events published in relation to a session's lifecycle. Each
// represents a fact that already happened, never something expected to
// happen.
//
// RecordingStarted and RecordingFinished live in domains/audio instead,
// since the Recording Manager that publishes them lives there: the domain
// that publishes an event owns its definition.

struct ErrorOccurred {
    // Absent when the error is not tied to a specific session (e.g. a
    // configuration load failure at startup).
    std::optional<SessionId> session_id;
    std::string component;
    std::string message;
};

} // namespace yoru::session
