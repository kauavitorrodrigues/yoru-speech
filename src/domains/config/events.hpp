#pragma once

#include "domains/config/configuration.hpp"

namespace yoru::config {

// Configuration is global to the service instance, never session-scoped.
// Published by the Configuration Manager whenever the active configuration
// changes.
struct ConfigurationChanged {
    Configuration configuration;
};

} // namespace yoru::config
