#pragma once

#include <filesystem>
#include <string>

namespace yoru::speech {

// Size category of a recognition model, following whisper.cpp's naming.
enum class ModelSize {
    Tiny,
    Base,
    Small,
    Medium,
    Large,
};

// Represents an available recognition model. A Model does not perform
// recognition itself — it only describes a resource used by a Speech
// Backend. Models are reusable and never belong to a specific session.
struct Model {
    std::string name;
    ModelSize size = ModelSize::Base;
    std::string supported_language;
    std::filesystem::path path;

    // Identifier of the backend this model is compatible with (e.g.
    // "whisper.cpp"). Kept as a string, not an enum, so new backends can be
    // introduced (ADR-004) without modifying this type.
    std::string backend;
};

} // namespace yoru::speech
