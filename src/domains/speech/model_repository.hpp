#pragma once

#include "domains/speech/model.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yoru::speech {

// Resolves the default models directory, honoring XDG_DATA_HOME when set,
// falling back to ~/.local/share otherwise.
std::filesystem::path default_models_path();

// Scans `directory` for whisper.cpp `.bin` model files and returns their
// metadata, derived from each filename by convention (e.g.
// "ggml-tiny.en-q5_1.bin", split on "-"/"."/"_" into whole tokens): the
// name is the filename stem, the size category is the first of
// tiny/base/small/medium/large found among the tokens, and the supported
// language is "en" when "en" is one of the tokens, "multi" otherwise.
//
// A `.bin` file whose name contains none of the known size tokens is not
// a recognizable whisper.cpp model and is skipped, rather than guessed at.
//
// Returns an empty list, without error, when `directory` does not exist
// or contains no recognizable models: absence of models is not a failure
// on its own.
std::vector<Model> list_available_models(const std::filesystem::path& directory);

// Finds the model named `name` (its filename stem, see list_available_models)
// among `available`. Returns nullopt if none matches.
std::optional<Model> find_model(const std::vector<Model>& available, const std::string& name);

} // namespace yoru::speech
