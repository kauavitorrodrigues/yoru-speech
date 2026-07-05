#pragma once

#include <string>

namespace yoru::config {

// Strategy for loading the speech recognition model into memory.
enum class ModelLoadPolicy {
    // Load the model only when a session needs it, release it afterwards.
    // Minimizes idle resource consumption.
    OnDemand,
    // Keep the model loaded in memory for the lifetime of the service.
    // Trades idle memory for lower latency on each session.
    AlwaysLoaded,
};

// Represents the user's persisted preferences. Configuration expresses
// intent — it never carries execution state.
struct Configuration {
    std::string default_language = "auto";
    std::string selected_model = "base";
    bool auto_clipboard = true;
    ModelLoadPolicy model_load_policy = ModelLoadPolicy::OnDemand;
};

} // namespace yoru::config
