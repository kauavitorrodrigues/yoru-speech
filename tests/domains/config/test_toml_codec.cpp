#include "domains/config/toml_codec.hpp"

#include <doctest/doctest.h>

using yoru::config::Configuration;
using yoru::config::decode_toml;
using yoru::config::encode_toml;
using yoru::config::ModelLoadPolicy;
using yoru::config::validate;

TEST_CASE("validate accepts the default configuration") {
    CHECK(validate(Configuration{}).empty());
}

TEST_CASE("validate rejects an unrecognized language code") {
    Configuration config;
    config.default_language = "not-a-code";

    const auto errors = validate(config);

    REQUIRE(errors.size() == 1);
    CHECK(errors[0].field == "default_language");
}

TEST_CASE("validate accepts \"auto\" and 2-letter codes") {
    Configuration config;
    config.default_language = "auto";
    CHECK(validate(config).empty());

    config.default_language = "pt";
    CHECK(validate(config).empty());
}

TEST_CASE("validate rejects an empty model name") {
    Configuration config;
    config.selected_model = "";

    const auto errors = validate(config);

    REQUIRE(errors.size() == 1);
    CHECK(errors[0].field == "selected_model");
}

TEST_CASE("encode/decode round-trips a fully populated configuration") {
    Configuration original;
    original.default_language = "pt";
    original.selected_model = "small";
    original.auto_clipboard = false;
    original.model_load_policy = ModelLoadPolicy::AlwaysLoaded;
    original.transcription_prompt = "mixing English terms into Portuguese speech";

    const auto decoded = decode_toml(encode_toml(original), Configuration{});

    CHECK(decoded.errors.empty());
    CHECK(decoded.configuration.default_language == "pt");
    CHECK(decoded.configuration.selected_model == "small");
    CHECK(decoded.configuration.auto_clipboard == false);
    CHECK(decoded.configuration.model_load_policy == ModelLoadPolicy::AlwaysLoaded);
    CHECK(decoded.configuration.transcription_prompt ==
         "mixing English terms into Portuguese speech");
}

TEST_CASE("decode_toml merges missing fields with the given defaults") {
    Configuration defaults;
    defaults.selected_model = "medium";

    const auto decoded = decode_toml("default_language = \"pt\"\n", defaults);

    CHECK(decoded.errors.empty());
    CHECK(decoded.configuration.default_language == "pt");
    CHECK(decoded.configuration.selected_model == "medium");
}

TEST_CASE("decode_toml ignores unknown top-level keys") {
    const auto decoded =
        decode_toml("unknown_field = 42\ndefault_language = \"pt\"\n", Configuration{});

    CHECK(decoded.errors.empty());
    CHECK(decoded.configuration.default_language == "pt");
}

TEST_CASE("decode_toml falls back to defaults on a type mismatch and reports it") {
    const auto decoded = decode_toml("auto_clipboard = \"yes\"\n", Configuration{});

    REQUIRE(decoded.errors.size() == 1);
    CHECK(decoded.errors[0].field == "auto_clipboard");
    CHECK(decoded.configuration.auto_clipboard == Configuration{}.auto_clipboard);
}

TEST_CASE("decode_toml falls back to defaults entirely on unparseable syntax") {
    const auto decoded = decode_toml("this is not [ valid toml", Configuration{});

    REQUIRE(decoded.errors.size() == 1);
    CHECK(decoded.configuration.default_language == Configuration{}.default_language);
}

TEST_CASE("decode_toml rejects an unrecognized model_load_policy string") {
    const auto decoded = decode_toml("model_load_policy = \"sometimes\"\n", Configuration{});

    REQUIRE(decoded.errors.size() == 1);
    CHECK(decoded.errors[0].field == "model_load_policy");
    CHECK(decoded.configuration.model_load_policy == Configuration{}.model_load_policy);
}
