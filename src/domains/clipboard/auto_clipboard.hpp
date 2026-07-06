#pragma once

#include "core/event_bus.hpp"
#include "domains/clipboard/wl_clipboard_adapter.hpp"
#include "domains/config/configuration.hpp"

namespace yoru::clipboard {

// Subscribes to TranscriptionCompleted and copies the resulting text via
// a WlClipboardAdapter whenever Configuration::auto_clipboard is enabled,
// tracking that flag at runtime via ConfigurationChanged. Nothing else in
// the system knows this component exists: it is a pure EventBus
// consumer, exactly as the domain model specifies (neither the speech
// nor the session domain references it).
//
// `event_bus` must outlive this object: the constructor registers
// handlers capturing `this`, and EventBus::subscribe() has no
// unsubscribe (see its own documentation for the resulting lifetime
// contract).
class AutoClipboard {
public:
    // `initial_configuration` seeds whether copying starts enabled;
    // later changes are tracked via ConfigurationChanged, not by
    // re-reading configuration. `adapter` defaults to the real wl-copy
    // adapter; overridable for tests.
    AutoClipboard(core::EventBus& event_bus, const config::Configuration& initial_configuration,
                  WlClipboardAdapter adapter = WlClipboardAdapter{});

    AutoClipboard(const AutoClipboard&) = delete;
    AutoClipboard& operator=(const AutoClipboard&) = delete;
    AutoClipboard(AutoClipboard&&) = delete;
    AutoClipboard& operator=(AutoClipboard&&) = delete;

    bool is_enabled() const;

private:
    core::EventBus& event_bus_;
    WlClipboardAdapter adapter_;
    bool enabled_;
};

} // namespace yoru::clipboard
