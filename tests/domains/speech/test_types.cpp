#include "domains/speech/model.hpp"
#include "domains/speech/transcript.hpp"

#include <doctest/doctest.h>

using yoru::speech::Model;
using yoru::speech::ModelSize;
using yoru::speech::Transcript;

TEST_CASE("Transcript default-constructs to explicit, deterministic values") {
    const Transcript transcript;

    CHECK(transcript.text.empty());
    CHECK(transcript.detected_language.empty());
    CHECK(transcript.requested_language.empty());
    CHECK(transcript.audio_duration == std::chrono::milliseconds{0});
    CHECK(transcript.processing_time == std::chrono::milliseconds{0});
}

TEST_CASE("Transcript aggregate-initializes with the given fields") {
    const Transcript transcript{
        .text = "hello world",
        .detected_language = "en",
        .requested_language = "en",
        .audio_duration = std::chrono::milliseconds{2500},
        .processing_time = std::chrono::milliseconds{120},
    };

    CHECK(transcript.text == "hello world");
    CHECK(transcript.detected_language == "en");
    CHECK(transcript.audio_duration == std::chrono::milliseconds{2500});
}

TEST_CASE("Model default-constructs with a deterministic size category") {
    const Model model;

    CHECK(model.size == ModelSize::Base);
    CHECK(model.name.empty());
    CHECK(model.backend.empty());
}

TEST_CASE("Model aggregate-initializes with the given fields") {
    const Model model{
        .name = "ggml-tiny.en",
        .size = ModelSize::Tiny,
        .supported_language = "en",
        .path = "/models/ggml-tiny.en.bin",
        .backend = "whisper.cpp",
    };

    CHECK(model.name == "ggml-tiny.en");
    CHECK(model.size == ModelSize::Tiny);
    CHECK(model.backend == "whisper.cpp");
}
