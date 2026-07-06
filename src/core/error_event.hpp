#pragma once

#include "core/session_id.hpp"

#include <optional>
#include <string>

namespace yoru::core {

// A fact that some component failed, published by whichever component hit
// the failure. Lives in core, not any one domain, because many domains
// publish it and it carries no domain-specific fields, unlike events such
// as RecordingFinished or TranscriptionCompleted.
struct ErrorOccurred {
    // Absent when the error is not tied to a specific session (e.g. a
    // configuration load failure at startup).
    std::optional<SessionId> session_id;
    std::string component;
    std::string message;
};

} // namespace yoru::core
