#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace yoru::audio {

// The "Recording" from the domain model: the audio produced during a
// session. Not a file, not a device, just the samples and enough metadata
// to interpret them downstream. Produced by the Recording Manager, handed
// off to the Speech Engine for transcription.
struct Recording {
    std::vector<float> samples; // mono PCM float32
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;

    // Duration implied by the sample count and format. Zero when
    // sample_rate or channels is zero (a default-constructed Recording),
    // rather than dividing by zero.
    std::chrono::milliseconds duration() const;
};

} // namespace yoru::audio
