#include "domains/ipc/message_codec.hpp"

#include <nlohmann/json.hpp>

namespace yoru::ipc {

namespace {

using nlohmann::json;

std::string to_string(config::ModelLoadPolicy policy) {
    switch (policy) {
    case config::ModelLoadPolicy::OnDemand:
        return "on_demand";
    case config::ModelLoadPolicy::AlwaysLoaded:
        return "always_loaded";
    }
    return "on_demand";
}

std::optional<config::ModelLoadPolicy> parse_model_load_policy(const std::string& value) {
    if (value == "on_demand") {
        return config::ModelLoadPolicy::OnDemand;
    }
    if (value == "always_loaded") {
        return config::ModelLoadPolicy::AlwaysLoaded;
    }
    return std::nullopt;
}

std::string to_string(speech::ModelSize size) {
    switch (size) {
    case speech::ModelSize::Tiny:
        return "tiny";
    case speech::ModelSize::Base:
        return "base";
    case speech::ModelSize::Small:
        return "small";
    case speech::ModelSize::Medium:
        return "medium";
    case speech::ModelSize::Large:
        return "large";
    }
    return "base";
}

std::string to_string(session::ServiceState state) {
    switch (state) {
    case session::ServiceState::Idle:
        return "idle";
    case session::ServiceState::Recording:
        return "recording";
    case session::ServiceState::Processing:
        return "processing";
    case session::ServiceState::Error:
        return "error";
    }
    return "idle";
}

} // namespace

ParsedMessage parse_message(const std::string& line) {
    const json parsed = json::parse(line, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("type") ||
        !parsed.at("type").is_string()) {
        return ParsedMessage{
            .type = std::nullopt,
            .error = R"(message must be a JSON object with a string "type" field)",
        };
    }

    return ParsedMessage{.type = parsed.at("type").get<std::string>(), .error = std::nullopt};
}

DecodedSetConfig decode_set_config(const std::string& line, const config::Configuration& base) {
    const json parsed = json::parse(line, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return DecodedSetConfig{.configuration = base, .error = "message must be a JSON object"};
    }

    config::Configuration configuration = base;

    if (parsed.contains("default_language")) {
        if (!parsed.at("default_language").is_string()) {
            return DecodedSetConfig{.configuration = base,
                                    .error = "default_language must be a string"};
        }
        configuration.default_language = parsed.at("default_language").get<std::string>();
    }

    if (parsed.contains("selected_model")) {
        if (!parsed.at("selected_model").is_string()) {
            return DecodedSetConfig{.configuration = base,
                                    .error = "selected_model must be a string"};
        }
        configuration.selected_model = parsed.at("selected_model").get<std::string>();
    }

    if (parsed.contains("auto_clipboard")) {
        if (!parsed.at("auto_clipboard").is_boolean()) {
            return DecodedSetConfig{.configuration = base,
                                    .error = "auto_clipboard must be a boolean"};
        }
        configuration.auto_clipboard = parsed.at("auto_clipboard").get<bool>();
    }

    if (parsed.contains("model_load_policy")) {
        if (!parsed.at("model_load_policy").is_string()) {
            return DecodedSetConfig{.configuration = base,
                                    .error = "model_load_policy must be a string"};
        }
        const auto policy =
            parse_model_load_policy(parsed.at("model_load_policy").get<std::string>());
        if (!policy.has_value()) {
            return DecodedSetConfig{
                .configuration = base,
                .error = R"(model_load_policy must be "on_demand" or "always_loaded")",
            };
        }
        configuration.model_load_policy = policy.value();
    }

    return DecodedSetConfig{.configuration = configuration, .error = std::nullopt};
}

std::string encode_ack(const std::string& type) {
    return json{{"type", type}, {"ok", true}}.dump();
}

std::string encode_error(const std::string& type, const std::string& error) {
    return json{{"type", type}, {"ok", false}, {"error", error}}.dump();
}

std::string encode_transcript(const std::string& type, const speech::Transcript& transcript) {
    return json{
        {"type", type},
        {"ok", true},
        {"text", transcript.text},
        {"detected_language", transcript.detected_language},
        {"requested_language", transcript.requested_language},
        {"audio_duration_ms", transcript.audio_duration.count()},
        {"processing_time_ms", transcript.processing_time.count()},
    }
        .dump();
}

std::string encode_state(const std::string& type, session::ServiceState state) {
    return json{{"type", type}, {"ok", true}, {"state", to_string(state)}}.dump();
}

std::string encode_config(const std::string& type, const config::Configuration& configuration) {
    return json{
        {"type", type},
        {"ok", true},
        {"default_language", configuration.default_language},
        {"selected_model", configuration.selected_model},
        {"auto_clipboard", configuration.auto_clipboard},
        {"model_load_policy", to_string(configuration.model_load_policy)},
    }
        .dump();
}

std::string encode_models(const std::string& type, const std::vector<speech::Model>& models) {
    json models_json = json::array();
    for (const auto& model : models) {
        models_json.push_back(json{
            {"name", model.name},
            {"size", to_string(model.size)},
            {"supported_language", model.supported_language},
            {"path", model.path.string()},
            {"backend", model.backend},
        });
    }
    return json{{"type", type}, {"ok", true}, {"models", models_json}}.dump();
}

} // namespace yoru::ipc
