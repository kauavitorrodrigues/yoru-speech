#include "domains/clipboard/auto_clipboard.hpp"

#include "core/error_event.hpp"
#include "domains/config/events.hpp"
#include "domains/speech/events.hpp"

#include <utility>

namespace yoru::clipboard {

AutoClipboard::AutoClipboard(core::EventBus& event_bus,
                             const config::Configuration& initial_configuration,
                             WlClipboardAdapter adapter)
    : event_bus_(event_bus), adapter_(std::move(adapter)),
      enabled_(initial_configuration.auto_clipboard) {
    event_bus_.subscribe<speech::TranscriptionCompleted>(
        [this](const speech::TranscriptionCompleted& event) {
            if (!enabled_) {
                return;
            }

            if (const auto error = adapter_.copy(event.transcript.text)) {
                event_bus_.publish(core::ErrorOccurred{
                    .session_id = event.session_id,
                    .component = "clipboard",
                    .message = error->message,
                });
            }
        });

    event_bus_.subscribe<config::ConfigurationChanged>(
        [this](const config::ConfigurationChanged& event) {
            enabled_ = event.configuration.auto_clipboard;
        });
}

bool AutoClipboard::is_enabled() const {
    return enabled_;
}

} // namespace yoru::clipboard
