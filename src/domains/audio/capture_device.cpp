#include "domains/audio/capture_device.hpp"

#include <miniaudio/miniaudio.h>

#include <atomic>
#include <cassert>
#include <utility>

namespace yoru::audio {

namespace {

// The format whisper.cpp expects. Fixed on purpose: this project captures
// for exactly one downstream consumer, so there is no configuration to
// expose.
constexpr ma_format kCaptureFormat = ma_format_f32;
constexpr ma_uint32 kCaptureChannels = 1;
constexpr ma_uint32 kCaptureSampleRate = 16000;

// Runs on miniaudio's own audio thread, which is plain C and not
// exception-aware: an exception unwinding out of this function is
// undefined behavior. Any exception from the user callback is therefore
// swallowed here rather than propagated, degrading to a dropped buffer
// instead of a crash.
void forward_captured_samples(ma_device* device, void* /*output*/, const void* input,
                               ma_uint32 frame_count) {
    const auto* callback = static_cast<CaptureCallback*>(device->pUserData);
    // frame_count == sample count only because capture is mono
    // (kCaptureChannels == 1); revisit if that ever changes.
    try {
        (*callback)(static_cast<const float*>(input), frame_count);
    } catch (...) {
    }
}

} // namespace

struct CaptureDevice::Impl {
    CaptureCallback on_samples;
    ma_device device{};
    // Tracks whether start()/stop() were called, not hardware health: if
    // the OS stops the device from under us (e.g. mic unplugged), this
    // stays true until stop() is called.
    std::atomic<bool> running{false};
};

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

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = kCaptureFormat;
    config.capture.channels = kCaptureChannels;
    config.sampleRate = kCaptureSampleRate;
    config.dataCallback = forward_captured_samples;
    config.pUserData = &impl_->on_samples;

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        return CaptureError{"failed to open the default capture device"};
    }

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        return CaptureError{"failed to start the capture device"};
    }

    impl_->running = true;
    return std::nullopt;
}

void CaptureDevice::stop() {
    if (!impl_->running) {
        return;
    }
    ma_device_uninit(&impl_->device);
    impl_->running = false;
}

bool CaptureDevice::is_running() const {
    return impl_->running;
}

} // namespace yoru::audio
