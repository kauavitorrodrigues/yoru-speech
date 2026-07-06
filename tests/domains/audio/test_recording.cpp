#include "domains/audio/recording.hpp"

#include <doctest/doctest.h>

#include <chrono>

using yoru::audio::Recording;

TEST_CASE("Recording default-constructs to an empty, zero-duration buffer") {
    const Recording recording;

    CHECK(recording.samples.empty());
    CHECK(recording.duration() == std::chrono::milliseconds{0});
}

TEST_CASE("duration() derives from sample count, sample rate, and channels") {
    Recording recording;
    recording.sample_rate = 16000;
    recording.channels = 1;
    recording.samples.assign(8000, 0.0F); // half a second at 16kHz mono

    CHECK(recording.duration() == std::chrono::milliseconds{500});
}

TEST_CASE("duration() accounts for multiple channels") {
    Recording recording;
    recording.sample_rate = 16000;
    recording.channels = 2;
    recording.samples.assign(16000, 0.0F); // 8000 frames per channel at 16kHz

    CHECK(recording.duration() == std::chrono::milliseconds{500});
}
