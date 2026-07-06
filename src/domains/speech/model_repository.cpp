#include "domains/speech/model_repository.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace yoru::speech {

namespace {

std::filesystem::path home_directory() {
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home);
    }
    return {};
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Splits a lowercased filename on the delimiters whisper.cpp's own naming
// convention uses ("ggml-tiny.en-q5_1.bin"), so size/language markers are
// matched as whole tokens instead of raw substrings. Without this, a name
// like "notes-database.bin" would falsely match "base" as a substring of
// "database".
std::vector<std::string> split_tokens(const std::string& lower_filename) {
    std::vector<std::string> tokens;
    std::string current;
    for (const char c : lower_filename) {
        if (c == '-' || c == '.' || c == '_') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

// Returns nullopt when no token matches a known size, meaning the file
// doesn't follow whisper.cpp's naming convention and isn't treated as a
// recognizable model; see list_available_models.
std::optional<ModelSize> infer_size(const std::vector<std::string>& tokens) {
    // clang-format off
    static const std::unordered_map<std::string, ModelSize> kSizeTokens{
        {"tiny",   ModelSize::Tiny},
        {"base",   ModelSize::Base},
        {"small",  ModelSize::Small},
        {"medium", ModelSize::Medium},
        {"large",  ModelSize::Large},
    };
    // clang-format on

    for (const auto& token : tokens) {
        if (const auto it = kSizeTokens.find(token); it != kSizeTokens.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

std::string infer_supported_language(const std::vector<std::string>& tokens) {
    const bool english_only = std::find(tokens.begin(), tokens.end(), "en") != tokens.end();
    return english_only ? "en" : "multi";
}

} // namespace

std::filesystem::path default_models_path() {
    std::filesystem::path base;
    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME")) {
        base = xdg_data_home;
    } else {
        base = home_directory() / ".local" / "share";
    }
    return base / "yoru-speech" / "models";
}

std::vector<Model> list_available_models(const std::filesystem::path& directory) {
    std::vector<Model> models;

    std::error_code error_code;
    std::filesystem::directory_iterator it(directory, error_code);
    if (error_code) {
        return models;
    }
    const std::filesystem::directory_iterator end;

    // Advanced with the error_code overload of increment(), not operator++,
    // which would throw on a mid-scan enumeration failure (e.g. a file
    // removed underneath the scan) and break the no-throw contract below.
    for (; it != end; it.increment(error_code)) {
        if (error_code) {
            break;
        }

        const auto& entry = *it;
        if (entry.path().extension() != ".bin") {
            continue;
        }

        std::error_code file_type_error;
        if (!entry.is_regular_file(file_type_error) || file_type_error) {
            continue;
        }

        const auto tokens = split_tokens(to_lower(entry.path().filename().string()));
        const auto size = infer_size(tokens);
        if (!size.has_value()) {
            continue;
        }

        models.push_back(Model{
            .name = entry.path().stem().string(),
            .size = *size,
            .supported_language = infer_supported_language(tokens),
            .path = entry.path(),
            .backend = "whisper.cpp",
        });
    }

    return models;
}

std::optional<Model> find_model(const std::vector<Model>& available, const std::string& name) {
    const auto it = std::find_if(available.begin(), available.end(),
                                 [&](const Model& model) { return model.name == name; });
    return it != available.end() ? std::optional<Model>(*it) : std::nullopt;
}

} // namespace yoru::speech
