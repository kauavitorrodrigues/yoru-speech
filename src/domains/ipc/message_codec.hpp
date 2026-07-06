#pragma once

#include "domains/config/configuration.hpp"
#include "domains/session/service_state.hpp"
#include "domains/speech/model.hpp"
#include "domains/speech/transcript.hpp"

#include <optional>
#include <string>
#include <vector>

namespace yoru::ipc {

// A message parsed from one line of the JSON Lines protocol. `type`
// identifies the command or event it carries. Absent, with `error` set
// instead, when the line isn't valid JSON or has no string "type" field.
// Never throws: a malformed line produces an error, not an exception.
struct ParsedMessage {
    std::optional<std::string> type;
    std::optional<std::string> error;
};

ParsedMessage parse_message(const std::string& line);

// The outcome of decoding a set_config request's body into a
// Configuration. `error` is set, instead of `configuration` being
// meaningful, when a field has the wrong JSON type. Semantic validation
// (e.g. whether a language code is well-formed) is the Configuration
// Manager's job, not this decoder's: see config::validate().
struct DecodedSetConfig {
    config::Configuration configuration;
    std::optional<std::string> error;
};

// Decodes `line` as a set_config request, merged onto `base` for any
// field the request doesn't include: a client sending only the one field
// it wants to change must not silently reset every other field, the same
// merge-with-defaults contract config::decode_toml() follows for the
// on-disk format. Callers pass the current configuration as `base` so an
// omitted field is preserved, not reset to Configuration's hardcoded
// defaults.
DecodedSetConfig decode_set_config(const std::string& line, const config::Configuration& base);

// --- Response encoding ---
// Every response echoes back `type` from the request it answers, so a
// client can match responses to the commands it sent.

std::string encode_ack(const std::string& type);
std::string encode_error(const std::string& type, const std::string& error);
std::string encode_transcript(const std::string& type, const speech::Transcript& transcript);
std::string encode_state(const std::string& type, session::ServiceState state);
std::string encode_config(const std::string& type, const config::Configuration& configuration);
std::string encode_models(const std::string& type, const std::vector<speech::Model>& models);

} // namespace yoru::ipc
