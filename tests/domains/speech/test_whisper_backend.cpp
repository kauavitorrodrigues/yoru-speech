#include "domains/speech/whisper_backend.hpp"

#include "core/event_bus.hpp"
#include "domains/speech/events.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <numbers>
#include <optional>
#include <variant>

using yoru::core::EventBus;
using yoru::core::SessionId;
using yoru::speech::Model;
using yoru::speech::ModelLoaded;
using yoru::speech::ModelSize;
using yoru::speech::SpeechError;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionCompleted;
using yoru::speech::TranscriptionRequest;
using yoru::speech::TranscriptionStarted;
using yoru::speech::WhisperBackend;

namespace {

// Installed by the `whisper.cpp-model-tiny-q5_1` AUR package, used here as
// a real model to validate against instead of mocking whisper.cpp.
// Environments without it (e.g. a fresh CI host) skip the tests that need
// it rather than failing on a missing environment dependency.
const std::filesystem::path kTinyModelPath =
    "/usr/share/whisper.cpp-model-tiny-q5_1/ggml-tiny-q5_1.bin";

Model tiny_model() {
    return Model{
        .name = "ggml-tiny-q5_1",
        .size = ModelSize::Tiny,
        .supported_language = "multi",
        .path = kTinyModelPath,
        .backend = "whisper.cpp",
    };
}

// A one-second 440Hz tone: real, non-silent samples, without needing a
// microphone or a fixture with known speech content. Exercises the full
// whisper_full() pipeline; whether it recognizes words is not asserted
// here; see the deferred manual/integration test for that.
std::vector<float> one_second_tone() {
    constexpr int kSampleRate = 16000;
    constexpr float kFrequencyHz = 440.0F;

    std::vector<float> samples(kSampleRate);
    for (int i = 0; i < kSampleRate; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        samples[static_cast<std::size_t>(i)] =
            0.5F * std::sin(2.0F * std::numbers::pi_v<float> * kFrequencyHz * t);
    }
    return samples;
}

} // namespace

TEST_CASE("load_model() reports an error when the model file does not exist") {
    EventBus bus;
    WhisperBackend backend(bus);

    const Model model{
        .name = "missing",
        .size = ModelSize::Tiny,
        .supported_language = "auto",
        .path = "/nonexistent/model.bin",
        .backend = "whisper.cpp",
    };
    const auto error = backend.load_model(model);

    REQUIRE(error.has_value());
    CHECK_FALSE(backend.has_model_loaded());
}

TEST_CASE("transcribe() reports an error for an empty buffer, regardless of a loaded model") {
    EventBus bus;
    WhisperBackend backend(bus);

    const auto result = backend.transcribe(SessionId{1}, {}, TranscriptionRequest{});

    REQUIRE(std::holds_alternative<SpeechError>(result));
}

TEST_CASE("transcribe() reports an error when no model is loaded") {
    EventBus bus;
    WhisperBackend backend(bus);

    const auto result = backend.transcribe(SessionId{1}, one_second_tone(), TranscriptionRequest{});

    REQUIRE(std::holds_alternative<SpeechError>(result));
}

TEST_CASE("load_model() with a real model succeeds and publishes ModelLoaded") {
    if (!std::filesystem::exists(kTinyModelPath)) {
        MESSAGE("skipping: tiny model not installed (", kTinyModelPath.string(), ")");
        return;
    }

    EventBus bus;
    WhisperBackend backend(bus);

    std::optional<Model> loaded;
    bus.subscribe<ModelLoaded>([&](const ModelLoaded& event) { loaded = event.model; });

    const auto error = backend.load_model(tiny_model());

    REQUIRE_FALSE(error.has_value());
    CHECK(backend.has_model_loaded());
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "ggml-tiny-q5_1");
}

TEST_CASE("transcribe() with a real model produces a Transcript and publishes both events") {
    if (!std::filesystem::exists(kTinyModelPath)) {
        MESSAGE("skipping: tiny model not installed (", kTinyModelPath.string(), ")");
        return;
    }

    EventBus bus;
    WhisperBackend backend(bus);

    std::optional<SessionId> started_id;
    std::optional<Transcript> completed_transcript;
    bus.subscribe<TranscriptionStarted>(
        [&](const TranscriptionStarted& event) { started_id = event.session_id; });
    bus.subscribe<TranscriptionCompleted>(
        [&](const TranscriptionCompleted& event) { completed_transcript = event.transcript; });

    REQUIRE_FALSE(backend.load_model(tiny_model()).has_value());

    const auto result = backend.transcribe(SessionId{7}, one_second_tone(), TranscriptionRequest{});

    REQUIRE(std::holds_alternative<Transcript>(result));
    const auto& transcript = std::get<Transcript>(result);
    CHECK(transcript.requested_language == "auto");
    CHECK(transcript.audio_duration == std::chrono::milliseconds{1000});

    REQUIRE(started_id.has_value());
    CHECK(started_id.value() == SessionId{7});
    REQUIRE(completed_transcript.has_value());
    CHECK(completed_transcript->audio_duration == std::chrono::milliseconds{1000});
}
