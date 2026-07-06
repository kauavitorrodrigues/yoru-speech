#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace yoru::audio {

// The format captured audio is delivered in: mono PCM float32 at this
// sample rate, the format whisper.cpp expects. Exposed so callers that
// accumulate samples (e.g. the Recording Manager) can tag their buffers
// without duplicating these values; the actual sample type (float32) has
// no plain C++ representation to export beyond the CaptureCallback
// signature below.
inline constexpr std::uint32_t kSampleRate = 16000;
inline constexpr std::uint32_t kChannels = 1;

// An error opening or operating the capture device, with a message
// suitable for logging. Never thrown: reported through
// CaptureDevice::start()'s return value.
struct CaptureError {
    std::string message;
};

// Invoked once per captured buffer, from the capture backend's own thread,
// for as long as capture is running. `samples` are mono PCM float32.
using CaptureCallback = std::function<void(const float* samples, std::size_t count)>;

// Encapsulates the miniaudio capture API. This is the only translation
// unit in the project that includes <miniaudio.h>; nothing else in the
// system knows miniaudio exists.
//
// Captures from the system's default input device in the format
// whisper.cpp expects: PCM, 16kHz, mono, float32. That choice is made
// here, in the adapter, and never leaks into the public interface.
class CaptureDevice {
public:
    explicit CaptureDevice(CaptureCallback on_samples);
    ~CaptureDevice();

    // Non-copyable and non-movable. A move would technically be safe (the
    // Impl's heap address is stable), but there's no use case for it and
    // immovability is the safer default for a type wrapping a live device.
    CaptureDevice(const CaptureDevice&) = delete;
    CaptureDevice& operator=(const CaptureDevice&) = delete;
    CaptureDevice(CaptureDevice&&) = delete;
    CaptureDevice& operator=(CaptureDevice&&) = delete;

    // Opens the default capture device and starts streaming. Returns an
    // error if no device is available or the device fails to start;
    // already running is a no-op success.
    std::optional<CaptureError> start();

    // Stops streaming and releases the device. Safe to call when not
    // running (no-op). Also called from the destructor, so a CaptureDevice
    // destroyed mid-capture never leaks the device.
    void stop();

    bool is_running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace yoru::audio
