#pragma once

#include "core/event_bus.hpp"
#include "domains/config/configuration.hpp"
#include "domains/config/toml_codec.hpp"

#include <filesystem>
#include <vector>

namespace yoru::config {

// Resolves the configuration file path, honoring XDG_CONFIG_HOME when set,
// falling back to ~/.config otherwise.
std::filesystem::path default_config_path();

// Sole owner of the service's configuration in memory. Loads from and
// persists to disk, validates every change, and notifies the rest of the
// system through the EventBus. Clients never edit the configuration file
// directly.
class ConfigurationManager {
public:
    // `event_bus` must outlive this manager.
    ConfigurationManager(std::filesystem::path config_path, core::EventBus& event_bus);

    // Loads the configuration from disk, creating the file (and its parent
    // directories) with default values if it does not exist yet. Malformed
    // or invalid fields fall back to their default; loading never fails
    // outright. Returns any errors encountered along the way.
    std::vector<ValidationError> load();

    const Configuration& current() const;

    // Validates and applies `next`. Persists atomically *before* committing
    // it in memory: if validation or persistence fails, the current
    // configuration is left untouched, ConfigurationChanged is not
    // published, and the errors are returned.
    std::vector<ValidationError> update(Configuration next);

private:
    std::filesystem::path config_path_;
    core::EventBus& event_bus_;
    Configuration configuration_;
};

} // namespace yoru::config
