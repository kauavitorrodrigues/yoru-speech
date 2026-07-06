#include "domains/clipboard/auto_clipboard.hpp"

#include "core/error_event.hpp"
#include "domains/config/events.hpp"
#include "domains/speech/events.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>

using yoru::clipboard::AutoClipboard;
using yoru::clipboard::WlClipboardAdapter;
using yoru::config::Configuration;
using yoru::config::ConfigurationChanged;
using yoru::core::ErrorOccurred;
using yoru::core::EventBus;
using yoru::core::SessionId;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionCompleted;

namespace {

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

std::string unique_marker() {
    return "yoru-speech-auto-clipboard-test-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

TranscriptionCompleted completed_with(const std::string& text) {
    return TranscriptionCompleted{
        .session_id = SessionId{1},
        .transcript =
            Transcript{
                .text = text,
                .detected_language = "en",
                .requested_language = "auto",
                .audio_duration = std::chrono::milliseconds{0},
                .processing_time = std::chrono::milliseconds{0},
            },
    };
}

// These tests round-trip through the real wl-copy/wl-paste, which need an
// actual Wayland compositor. Skip gracefully rather than failing on an
// environment limitation these tests aren't meant to catch, mirroring
// WlClipboardAdapter's own test suite.
bool skip_if_no_clipboard() {
    const auto error = WlClipboardAdapter{}.copy("yoru-speech-clipboard-availability-check");
    if (error.has_value()) {
        MESSAGE("skipping: no working clipboard available (", error->message, ")");
        return true;
    }
    return false;
}

} // namespace

TEST_CASE("a TranscriptionCompleted is copied when auto_clipboard starts enabled") {
    if (skip_if_no_clipboard()) {
        return;
    }

    EventBus bus;
    Configuration configuration;
    configuration.auto_clipboard = true;
    AutoClipboard clipboard(bus, configuration);

    const std::string marker = unique_marker();
    bus.publish(completed_with(marker));

    CHECK(read_clipboard() == marker);
}

TEST_CASE("a TranscriptionCompleted is not copied when auto_clipboard starts disabled") {
    if (skip_if_no_clipboard()) {
        return;
    }

    EventBus bus;
    // Prime the clipboard with a known value first, so we can tell "not
    // overwritten" apart from "happens to already be empty".
    const std::string sentinel = unique_marker();
    REQUIRE_FALSE(WlClipboardAdapter{}.copy(sentinel).has_value());

    Configuration configuration;
    configuration.auto_clipboard = false;
    AutoClipboard clipboard(bus, configuration);

    bus.publish(completed_with(unique_marker()));

    CHECK(read_clipboard() == sentinel);
}

TEST_CASE("ConfigurationChanged toggles auto_clipboard at runtime") {
    if (skip_if_no_clipboard()) {
        return;
    }

    EventBus bus;
    Configuration configuration;
    configuration.auto_clipboard = false;
    AutoClipboard clipboard(bus, configuration);
    CHECK_FALSE(clipboard.is_enabled());

    Configuration enabled_configuration = configuration;
    enabled_configuration.auto_clipboard = true;
    bus.publish(ConfigurationChanged{.configuration = enabled_configuration});
    CHECK(clipboard.is_enabled());

    const std::string marker = unique_marker();
    bus.publish(completed_with(marker));
    CHECK(read_clipboard() == marker);

    Configuration disabled_configuration = configuration;
    disabled_configuration.auto_clipboard = false;
    bus.publish(ConfigurationChanged{.configuration = disabled_configuration});
    CHECK_FALSE(clipboard.is_enabled());
}

TEST_CASE("a copy failure publishes ErrorOccurred instead of crashing") {
    EventBus bus;
    Configuration configuration;
    configuration.auto_clipboard = true;
    AutoClipboard clipboard(bus, configuration,
                            WlClipboardAdapter("definitely-not-a-real-clipboard-command-xyz"));

    std::optional<ErrorOccurred> error_event;
    bus.subscribe<ErrorOccurred>([&](const ErrorOccurred& event) { error_event = event; });

    bus.publish(completed_with("this copy will fail"));

    REQUIRE(error_event.has_value());
    CHECK(error_event->component == "clipboard");
    CHECK(error_event->session_id.has_value());
    CHECK(error_event->session_id.value() == SessionId{1});
}
