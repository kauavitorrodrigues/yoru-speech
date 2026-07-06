#include "domains/ipc/event_bridge.hpp"

#include "core/error_event.hpp"
#include "domains/audio/events.hpp"
#include "domains/config/events.hpp"
#include "domains/ipc/message_codec.hpp"
#include "domains/session/events.hpp"
#include "domains/speech/events.hpp"

namespace yoru::ipc {

template <typename Event> void EventBridge::broadcast(const Event& event) {
    if (subscribed_clients_.empty()) {
        return;
    }
    const std::string line = encode_event(event);
    for (const IpcServer::ClientId client_id : subscribed_clients_) {
        server_.send_line(client_id, line);
    }
}

EventBridge::EventBridge(core::EventBus& event_bus, IpcServer& server)
    : event_bus_(event_bus), server_(server) {
    event_bus_.subscribe<audio::RecordingStarted>(
        [this](const audio::RecordingStarted& event) { broadcast(event); });
    event_bus_.subscribe<audio::RecordingFinished>(
        [this](const audio::RecordingFinished& event) { broadcast(event); });
    event_bus_.subscribe<speech::TranscriptionStarted>(
        [this](const speech::TranscriptionStarted& event) { broadcast(event); });
    event_bus_.subscribe<speech::TranscriptionCompleted>(
        [this](const speech::TranscriptionCompleted& event) { broadcast(event); });
    event_bus_.subscribe<speech::ModelLoaded>(
        [this](const speech::ModelLoaded& event) { broadcast(event); });
    event_bus_.subscribe<config::ConfigurationChanged>(
        [this](const config::ConfigurationChanged& event) { broadcast(event); });
    event_bus_.subscribe<core::ErrorOccurred>(
        [this](const core::ErrorOccurred& event) { broadcast(event); });
    event_bus_.subscribe<session::SessionCancelled>(
        [this](const session::SessionCancelled& event) { broadcast(event); });
}

void EventBridge::subscribe(IpcServer::ClientId client_id) {
    subscribed_clients_.insert(client_id);
}

void EventBridge::unsubscribe(IpcServer::ClientId client_id) {
    subscribed_clients_.erase(client_id);
}

bool EventBridge::is_subscribed(IpcServer::ClientId client_id) const {
    return subscribed_clients_.contains(client_id);
}

} // namespace yoru::ipc
