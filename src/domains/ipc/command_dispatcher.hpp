#pragma once

#include "domains/config/configuration_manager.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/model_repository.hpp"

#include <filesystem>
#include <string>

namespace yoru::ipc {

// Translates IPC messages into calls on the Session Manager and
// Configuration Manager, and their results back into response messages.
// Implements no domain logic itself, only translation: it never touches
// ServiceState, Configuration validation, or model files directly, only
// through those two managers (and, for list_models, the same
// list_available_models() the Speech Engine's own model management uses).
class CommandDispatcher {
public:
    // `session_manager` and `configuration_manager` must outlive this
    // dispatcher. `models_directory` is the directory list_models scans.
    explicit CommandDispatcher(
        session::SessionManager& session_manager,
        config::ConfigurationManager& configuration_manager,
        std::filesystem::path models_directory = speech::default_models_path());

    // Parses and executes `line` as a single request, returning the
    // response line to send back to the client that sent it. Malformed
    // messages and unrecognized commands produce a clear error response
    // instead of crashing or being silently ignored.
    std::string handle_line(const std::string& line) const;

private:
    session::SessionManager& session_manager_;
    config::ConfigurationManager& configuration_manager_;
    std::filesystem::path models_directory_;
};

} // namespace yoru::ipc
