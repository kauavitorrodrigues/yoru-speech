#include "domains/audio/capture_device.hpp"

#include <miniaudio/miniaudio.h>

#include <atomic>
#include <cassert>
#include <utility>

namespace yoru::audio {

struct CaptureDevice::Impl {
    CaptureCallback on_samples;
    ma_device device{};
    // Whether the hardware is (as far as we know) actively streaming.
    // Cleared not just by stop(), but also by the notification callback
    // below when the OS stops the device out from under us (e.g. mic
    // unplugged) — so this reflects real hardware state, not just "did
    // someone call start()/stop()".
    std::atomic<bool> running{false};
    // Whether `device` currently holds a live miniaudio device that
    // needs ma_device_uninit(). Deliberately separate from `running`:
    // an unplug clears `running` immediately (from miniaudio's thread)
    // while the device object itself is still fully initialized and
    // must still be released via ma_device_uninit(), exactly once, by
    // stop(). Only ever touched from the controlling thread (start()/
    // stop()), never from a callback, so it doesn't need to be atomic.
    bool initialized = false;
};

namespace {

// miniaudio's own format enum has no plain C++ representation, so it stays
// private to this translation unit; kSampleRate/kChannels are shared with
// the rest of the domain through the public header.
constexpr ma_format kCaptureFormat = ma_format_f32;

// Runs on miniaudio's own audio thread, which is plain C and not
// exception-aware: an exception unwinding out of this function is
// undefined behavior. Any exception from the user callback is therefore
// swallowed here rather than propagated, degrading to a dropped buffer
// instead of a crash.
void forward_captured_samples(ma_device* device, void* /*output*/, const void* input,
                              ma_uint32 frame_count) {
    auto* impl = static_cast<CaptureDevice::Impl*>(device->pUserData);
    // frame_count == sample count only because capture is mono
    // (kChannels == 1); revisit if that ever changes.
    try {
        impl->on_samples(static_cast<const float*>(input), frame_count);
    } catch (...) {}
}

// Runs on miniaudio's own thread, same constraints as
// forward_captured_samples above. miniaudio fires
// ma_device_notification_type_stopped both when the app calls stop()/
// uninit() and when the hardware disappears from under it (e.g. mic
// unplugged); either way, clearing `running` here keeps is_running()
// truthful about hardware state instead of only "did someone call
// start()/stop()", so a caller polling it after an unplug sees the
// device is actually gone rather than believing capture is still live.
void handle_device_notification(const ma_device_notification* notification) {
    if (notification->type != ma_device_notification_type_stopped) {
        return;
    }
    auto* impl = static_cast<CaptureDevice::Impl*>(notification->pDevice->pUserData);
    impl->running = false;
}

} // namespace

CaptureDevice::CaptureDevice(CaptureCallback on_samples) : impl_(std::make_unique<Impl>()) {
    assert(on_samples && "CaptureDevice requires a non-null capture callback");
    impl_->on_samples = std::move(on_samples);
}

CaptureDevice::~CaptureDevice() {
    stop();
}

std::optional<CaptureError> CaptureDevice::start() {
    if (impl_->running) {
        return std::nullopt;
    }
    if (!impl_->on_samples) {
        return CaptureError{"capture callback is not set"};
    }

    if (impl_->initialized) {
        // The previous device object is still allocated (e.g. the
        // hardware disappeared and running was cleared, but stop() was
        // never called to release it) — tear it down first so
        // ma_device_init() below never runs over a still-live device.
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = kCaptureFormat;
    config.capture.channels = kChannels;
    config.sampleRate = kSampleRate;
    config.dataCallback = forward_captured_samples;
    config.notificationCallback = handle_device_notification;
    config.pUserData = impl_.get();

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        return CaptureError{"failed to open the default capture device"};
    }
    impl_->initialized = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
        return CaptureError{"failed to start the capture device"};
    }

    impl_->running = true;
    return std::nullopt;
}

void CaptureDevice::stop() {
    if (!impl_->initialized) {
        return;
    }
    ma_device_uninit(&impl_->device);
    impl_->initialized = false;
    impl_->running = false;
}

bool CaptureDevice::is_running() const {
    return impl_->running;
}

} // namespace yoru::audio
