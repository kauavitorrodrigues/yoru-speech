#include "domains/clipboard/wl_clipboard_adapter.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <string>

using yoru::clipboard::WlClipboardAdapter;

namespace {

// Reads the current Wayland clipboard content via the real wl-paste
// binary, the same tool a real desktop environment would use to verify
// a copy actually landed. Real dependency, matching this project's
// established preference for exercising the real external tool over
// mocking it (see CaptureDevice's real microphone, WhisperBackend's real
// model).
std::string read_clipboard() {
    FILE* pipe = ::popen("wl-paste -n 2>/dev/null", "r");
    REQUIRE(pipe != nullptr);

    std::string output;
    std::array<char, 256> chunk{};
    std::size_t bytes_read = 0;
    while ((bytes_read = std::fread(chunk.data(), 1, chunk.size(), pipe)) > 0) {
        output.append(chunk.data(), bytes_read);
    }
    ::pclose(pipe);
    return output;
}

// The real wl-copy tests need an actual Wayland compositor to talk to
// (this environment has one; a headless CI runner might not). Skip
// gracefully rather than failing on an environment limitation these
// tests aren't meant to catch, matching the pattern already established
// for RecordingManager's real-microphone tests and WhisperBackend's
// real-model tests.
bool skip_if_no_clipboard() {
    const auto error = WlClipboardAdapter{}.copy("yoru-speech-clipboard-availability-check");
    if (error.has_value()) {
        MESSAGE("skipping: no working clipboard available (", error->message, ")");
        return true;
    }
    return false;
}

} // namespace

TEST_CASE("copy() with the real wl-copy sets the system clipboard") {
    if (skip_if_no_clipboard()) {
        return;
    }

    const WlClipboardAdapter adapter;
    const std::string marker =
        "yoru-speech-clipboard-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    const auto error = adapter.copy(marker);

    REQUIRE_FALSE(error.has_value());
    CHECK(read_clipboard() == marker);
}

TEST_CASE("copy() with a nonexistent command reports it wasn't found") {
    const WlClipboardAdapter adapter("definitely-not-a-real-clipboard-command-xyz");

    const auto error = adapter.copy("hello");

    REQUIRE(error.has_value());
    CHECK(error->message.find("not found") != std::string::npos);
}

TEST_CASE("copy() with a command that exits non-zero reports the exit status") {
    const WlClipboardAdapter adapter("/usr/bin/false");

    const auto error = adapter.copy("hello");

    REQUIRE(error.has_value());
    CHECK(error->message.find("exited with status") != std::string::npos);
}

TEST_CASE("copy() with a command that never exits times out and is killed") {
    // "yes" ignores stdin and prints forever until killed: a
    // deterministic stand-in for a hung wl-copy, without needing to
    // actually wedge the real one.
    const WlClipboardAdapter adapter("yes", std::chrono::milliseconds{100});

    const auto error = adapter.copy("hello");

    REQUIRE(error.has_value());
    CHECK(error->message.find("timed out") != std::string::npos);
}

TEST_CASE("copy() with a command that exits zero reports success") {
    const WlClipboardAdapter adapter("/usr/bin/true");

    const auto error = adapter.copy("hello");

    CHECK_FALSE(error.has_value());
}
