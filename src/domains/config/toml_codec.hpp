#pragma once

#include "domains/config/configuration.hpp"

#include <string>
#include <vector>

namespace yoru::config {

// A single field that failed validation or decoding, with a message
// suitable for logging. Never thrown: always reported through
// DecodedConfiguration::errors or validate()'s return value.
struct ValidationError {
    std::string field;
    std::string message;
};

struct DecodedConfiguration {
    Configuration configuration;
    std::vector<ValidationError> errors;
};

// Checks semantic constraints on an already-typed Configuration (e.g.
// language code format, non-empty model name). Returns an empty vector
// when every field is valid.
std::vector<ValidationError> validate(const Configuration& configuration);

// Serializes `configuration` as a TOML document.
std::string encode_toml(const Configuration& configuration);

// Decodes `toml_source`, merged with `defaults` for any field that is
// missing, malformed, or fails validate(). Unknown top-level keys are
// ignored (forward compatibility). Never throws: a syntactically invalid
// document falls back entirely to `defaults`, reported as a single error.
DecodedConfiguration decode_toml(const std::string& toml_source, const Configuration& defaults);

} // namespace yoru::config
