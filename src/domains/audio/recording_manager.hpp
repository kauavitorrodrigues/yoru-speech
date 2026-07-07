#pragma once

#include "core/event_bus.hpp"
#include "core/session_id.hpp"
#include "domains/audio/capture_device.hpp"
#include "domains/audio/recording.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace yoru::audio {

// An error starting or stopping a recording, with a message suitable for
// logging. Never thrown: reported through start()/stop()'s return value.
struct RecordingError {
    std::string message;
};

// The outcome of stop(): exactly one of the finished Recording or the
// reason it could not be produced.
using RecordingResult = std::variant<Recording, RecordingError>;

// Orchestrates microphone capture for one recording at a time. Owns a
// CaptureDevice, accumulates its output into a Recording, and publishes
// RecordingStarted/RecordingFinished through the EventBus. Knows nothing
// about speech recognition: that responsibility belongs to the Speech
// Engine, introduced in a later phase.
//
// start()/stop()/is_recording() must be called from a single controlling
// thread: active_session_ is neither atomic nor lock-guarded. Only sample
// accumulation is shared with the capture callback thread.
class RecordingManager {
public:
    // `event_bus` must outlive this manager.
    explicit RecordingManager(core::EventBus& event_bus);

    // Non-copyable and non-movable, matching CaptureDevice, which it owns
    // by value.
    RecordingManager(const RecordingManager&) = delete;
    RecordingManager& operator=(const RecordingManager&) = delete;
    RecordingManager(RecordingManager&&) = delete;
    RecordingManager& operator=(RecordingManager&&) = delete;

    // Opens the default capture device and starts accumulating samples for
    // `session_id`. Publishes RecordingStarted on success. Returns an
    // error, without side effects, if a recording is already in progress
    // or the device cannot be opened.
    std::optional<RecordingError> start(core::SessionId session_id);

    // Stops capture and finalizes the Recording. Returns it directly (for
    // a caller orchestrating the next step synchronously, e.g. the
    // Session Manager) and also publishes RecordingFinished with the same
    // Recording (for decoupled subscribers). Returns an error, without
    // side effects, if no recording is currently in progress.
    RecordingResult stop();

    bool is_recording() const;

    // A copy of every sample captured so far in the recording currently in
    // progress, from `start_index` onward (0 = everything), without
    // stopping it. Empty if no recording is active or `start_index` is at
    // or beyond what's been captured so far. For a component that needs
    // to observe the audio while it's still being captured (e.g. the Live
    // Transcriber re-transcribing the not-yet-committed tail of a
    // recording on a timer) rather than only once the full Recording is
    // finalized by stop(). `start_index` lets such a caller re-read only
    // the portion it hasn't already committed, instead of paying to copy
    // and hold the lock over the entire buffer on every call.
    std::vector<float> samples_snapshot(std::size_t start_index) const;

private:
    void on_samples(const float* samples, std::size_t count);

    core::EventBus& event_bus_;
    CaptureDevice device_;
    mutable std::mutex samples_mutex_;
    std::vector<float> samples_;
    std::optional<core::SessionId> active_session_;
};

} // namespace yoru::audio
