#include "domains/config/configuration.hpp"

#include <doctest/doctest.h>

using yoru::config::Configuration;
using yoru::config::ModelLoadPolicy;

TEST_CASE("Configuration default-constructs with sensible defaults") {
    const Configuration config;

    CHECK(config.default_language == "auto");
    CHECK(config.selected_model == "base");
    CHECK(config.auto_clipboard == true);
    CHECK(config.model_load_policy == ModelLoadPolicy::OnDemand);
    CHECK(config.transcription_prompt.empty());
}

TEST_CASE("Configuration aggregate-initializes with the given fields") {
    const Configuration config{
        .default_language = "pt",
        .selected_model = "small",
        .auto_clipboard = false,
        .model_load_policy = ModelLoadPolicy::AlwaysLoaded,
        .transcription_prompt = "mixing English terms into Portuguese speech",
    };

    CHECK(config.default_language == "pt");
    CHECK(config.auto_clipboard == false);
    CHECK(config.model_load_policy == ModelLoadPolicy::AlwaysLoaded);
    CHECK(config.transcription_prompt == "mixing English terms into Portuguese speech");
}
