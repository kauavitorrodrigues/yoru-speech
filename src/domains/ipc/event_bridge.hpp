#pragma once

#include "core/event_bus.hpp"
#include "domains/ipc/ipc_server.hpp"

#include <unordered_set>

namespace yoru::ipc {

// Bridges EventBus facts to connected IPC clients that opted in. Every
// domain event (RecordingStarted, RecordingFinished, TranscriptionStarted,
// TranscriptionCompleted, TranscriptionPartial, ModelLoaded,
// ConfigurationChanged, ErrorOccurred, SessionCancelled) is encoded (see
// message_codec.hpp) and pushed to every subscribed client via
// IpcServer::send_line() as soon as it's published. Implements no domain
// logic: only serializes and fans out facts the rest of the system
// already produced.
//
// Clients are NOT subscribed by default: subscribe()/unsubscribe() are
// meant to be called in response to a client's own request message (see
// IpcService), matching the roadmap's "clientes optem por receber
// eventos" (opt-in, not opt-out).
//
// Every method, and the EventBus subscriptions registered by the
// constructor, must run on the same single controlling thread as the
// rest of the IPC stack: a subscriber invoked from EventBus::publish()
// calls send_line() synchronously, from whatever thread published the
// event in the first place.
class EventBridge {
public:
    // `event_bus` and `server` must outlive this bridge. Stronger than it
    // sounds: EventBus::subscribe() has no unsubscribe (see its own
    // docs), so the 9 handlers this constructor registers stay live for
    // event_bus's entire remaining lifetime, each capturing `this`. No
    // event may be published on `event_bus` after this bridge is
    // destroyed: doing so would invoke a callback into a destroyed
    // object. The composition root must destroy this bridge (directly,
    // or via whatever owns it, e.g. IpcService) before event_bus, and
    // must not publish from any destructor that runs afterward.
    EventBridge(core::EventBus& event_bus, IpcServer& server);

    EventBridge(const EventBridge&) = delete;
    EventBridge& operator=(const EventBridge&) = delete;
    EventBridge(EventBridge&&) = delete;
    EventBridge& operator=(EventBridge&&) = delete;

    // Opts `client_id` in to receiving every event pushed from here on.
    // Safe to call for an already-subscribed client (no-op).
    void subscribe(IpcServer::ClientId client_id);

    // Opts `client_id` back out. Safe to call for a client that was
    // never subscribed (no-op).
    void unsubscribe(IpcServer::ClientId client_id);

    bool is_subscribed(IpcServer::ClientId client_id) const;

private:
    template <typename Event> void broadcast(const Event& event);

    core::EventBus& event_bus_;
    IpcServer& server_;
    std::unordered_set<IpcServer::ClientId> subscribed_clients_;
};

} // namespace yoru::ipc
