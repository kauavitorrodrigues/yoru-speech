#include "domains/audio/capture_device.hpp"

#include <doctest/doctest.h>

using yoru::audio::CaptureDevice;

TEST_CASE("CaptureDevice is not running before start() is called") {
    const CaptureDevice device([](const float*, std::size_t) {});

    CHECK_FALSE(device.is_running());
}

TEST_CASE("CaptureDevice destroyed without starting does not crash") {
    CaptureDevice device([](const float*, std::size_t) {});
}
