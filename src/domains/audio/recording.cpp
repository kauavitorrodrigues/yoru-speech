#include "domains/audio/recording.hpp"

namespace yoru::audio {

std::chrono::milliseconds Recording::duration() const {
    if (sample_rate == 0 || channels == 0) {
        return std::chrono::milliseconds{0};
    }

    // Widen to int64_t before multiplying by 1000 so this stays correct on
    // platforms where size_t is 32 bits, not just the 64-bit targets this
    // service runs on today.
    const auto frame_count = static_cast<std::int64_t>(samples.size() / channels);
    const auto total_ms = (frame_count * 1000) / sample_rate;
    return std::chrono::milliseconds{total_ms};
}

} // namespace yoru::audio
