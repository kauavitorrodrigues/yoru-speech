#include "domains/config/toml_codec.hpp"

#include <toml++/toml.hpp>

#include <cctype>
#include <optional>
#include <sstream>

// The only translation unit in the project allowed to include toml++
// Every other module goes through this codec.

namespace yoru::config {

namespace {

// Accepts "auto" or a 2-letter lowercase code (e.g. "pt", "en"). Does not
// check against a real ISO 639 list, since that would need a database this
// project doesn't otherwise carry, nor does it accept 3-letter (ISO
// 639-2) or uppercase codes.
constexpr const char* kLanguageCodeMessage = R"(must be "auto" or a 2-letter language code)";
constexpr const char* kModelNameMessage = "must not be empty";
constexpr const char* kModelLoadPolicyMessage = R"(must be "on_demand" or "always_loaded")";

bool is_valid_language_code(const std::string& value) {
    if (value == "auto") {
        return true;
    }
    if (value.size() != 2) {
        return false;
    }
    return std::islower(static_cast<unsigned char>(value[0])) != 0 &&
           std::islower(static_cast<unsigned char>(value[1])) != 0;
}

bool is_valid_model_name(const std::string& value) {
    return !value.empty();
}

std::string to_string(ModelLoadPolicy policy) {
    switch (policy) {
    case ModelLoadPolicy::OnDemand:
        return "on_demand";
    case ModelLoadPolicy::AlwaysLoaded:
        return "always_loaded";
    }
    return "on_demand";
}

std::optional<ModelLoadPolicy> parse_model_load_policy(const std::string& value) {
    if (value == "on_demand") {
        return ModelLoadPolicy::OnDemand;
    }
    if (value == "always_loaded") {
        return ModelLoadPolicy::AlwaysLoaded;
    }
    return std::nullopt;
}

} // namespace

std::vector<ValidationError> validate(const Configuration& configuration) {
    std::vector<ValidationError> errors;

    if (!is_valid_language_code(configuration.default_language)) {
        errors.push_back({"default_language", kLanguageCodeMessage});
    }
    if (!is_valid_model_name(configuration.selected_model)) {
        errors.push_back({"selected_model", kModelNameMessage});
    }

    return errors;
}

std::string encode_toml(const Configuration& configuration) {
    toml::table root;
    root.insert("default_language", configuration.default_language);
    root.insert("selected_model", configuration.selected_model);
    root.insert("auto_clipboard", configuration.auto_clipboard);
    root.insert("model_load_policy", to_string(configuration.model_load_policy));

    std::ostringstream out;
    out << root;
    return out.str();
}

DecodedConfiguration decode_toml(const std::string& toml_source, const Configuration& defaults) {
    toml::table root;
    try {
        root = toml::parse(toml_source);
    } catch (const toml::parse_error& error) {
        return DecodedConfiguration{
            defaults,
            {ValidationError{"<file>", std::string(error.description())}},
        };
    }

    Configuration configuration = defaults;
    std::vector<ValidationError> errors;

    if (root.contains("default_language")) {
        if (auto value = root["default_language"].value<std::string>()) {
            configuration.default_language = *value;
        } else {
            errors.push_back({"default_language", "expected a string"});
        }
    }

    if (root.contains("selected_model")) {
        if (auto value = root["selected_model"].value<std::string>()) {
            configuration.selected_model = *value;
        } else {
            errors.push_back({"selected_model", "expected a string"});
        }
    }

    if (root.contains("auto_clipboard")) {
        if (auto value = root["auto_clipboard"].value<bool>()) {
            configuration.auto_clipboard = *value;
        } else {
            errors.push_back({"auto_clipboard", "expected a boolean"});
        }
    }

    if (root.contains("model_load_policy")) {
        if (auto value = root["model_load_policy"].value<std::string>()) {
            if (auto policy = parse_model_load_policy(*value)) {
                configuration.model_load_policy = *policy;
            } else {
                errors.push_back({"model_load_policy", kModelLoadPolicyMessage});
            }
        } else {
            errors.push_back({"model_load_policy", "expected a string"});
        }
    }

    // Semantic validation on the fully-merged configuration: any field that
    // fails falls back to its default, and the failure is reported. Reuses
    // the same predicates and messages as validate() so the two can never
    // drift apart.
    if (!is_valid_language_code(configuration.default_language)) {
        errors.push_back({"default_language", kLanguageCodeMessage});
        configuration.default_language = defaults.default_language;
    }
    if (!is_valid_model_name(configuration.selected_model)) {
        errors.push_back({"selected_model", kModelNameMessage});
        configuration.selected_model = defaults.selected_model;
    }

    return DecodedConfiguration{configuration, errors};
}

} // namespace yoru::config
