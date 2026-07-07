#include "domains/audio/recording_manager.hpp"

#include "domains/audio/events.hpp"
#include "domains/audio/recording.hpp"

#include <utility>

namespace yoru::audio {

RecordingManager::RecordingManager(core::EventBus& event_bus)
    : event_bus_(event_bus),
      device_([this](const float* samples, std::size_t count) { on_samples(samples, count); }) {}

void RecordingManager::on_samples(const float* samples, std::size_t count) {
    // Runs on the capture backend's audio thread, concurrently with
    // start()/stop() on the caller's thread.
    const std::lock_guard lock(samples_mutex_);
    samples_.insert(samples_.end(), samples, samples + count);
}

std::optional<RecordingError> RecordingManager::start(core::SessionId session_id) {
    if (active_session_.has_value()) {
        return RecordingError{"a recording is already in progress"};
    }

    {
        const std::lock_guard lock(samples_mutex_);
        samples_.clear();
    }

    if (const auto error = device_.start()) {
        return RecordingError{error->message};
    }

    active_session_ = session_id;
    event_bus_.publish(RecordingStarted{session_id});
    return std::nullopt;
}

RecordingResult RecordingManager::stop() {
    if (!active_session_.has_value()) {
        return RecordingError{"no recording is in progress"};
    }

    // Checked before stop(), which always leaves is_running() false: this
    // is the only way to tell "the device was already gone when we got
    // here" (e.g. the microphone was unplugged mid-recording) apart from
    // an ordinary, requested stop.
    const bool device_was_lost = !device_.is_running();
    device_.stop();

    if (device_was_lost) {
        active_session_.reset();
        {
            const std::lock_guard lock(samples_mutex_);
            samples_.clear();
        }
        return RecordingError{"capture device stopped unexpectedly (microphone disconnected?)"};
    }

    Recording recording;
    recording.sample_rate = kSampleRate;
    recording.channels = kChannels;
    {
        const std::lock_guard lock(samples_mutex_);
        recording.samples = std::move(samples_);
        samples_.clear();
    }

    const core::SessionId session_id = *active_session_;
    active_session_.reset();
    event_bus_.publish(RecordingFinished{session_id, recording});
    return recording;
}

bool RecordingManager::is_recording() const {
    return active_session_.has_value();
}

std::vector<float> RecordingManager::samples_snapshot(std::size_t start_index) const {
    const std::lock_guard lock(samples_mutex_);
    if (start_index >= samples_.size()) {
        return {};
    }
    return std::vector<float>(samples_.begin() + static_cast<std::ptrdiff_t>(start_index),
                              samples_.end());
}

} // namespace yoru::audio
