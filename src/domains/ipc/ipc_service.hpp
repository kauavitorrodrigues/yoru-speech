#pragma once

#include "core/event_bus.hpp"
#include "domains/config/configuration_manager.hpp"
#include "domains/ipc/command_dispatcher.hpp"
#include "domains/ipc/event_bridge.hpp"
#include "domains/ipc/ipc_server.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/model_repository.hpp"

#include <filesystem>

namespace yoru::ipc {

// Owns and wires together the full IPC stack: the socket transport
// (IpcServer), the command layer (CommandDispatcher), and event push
// (EventBridge). This is the only class the composition root needs to
// construct and drive to expose the service over its socket; it routes
// each incoming line to subscribe_events/unsubscribe_events or, failing
// that, to the CommandDispatcher, and cleans up a disconnected client's
// event subscription.
//
// Single-threaded, like every class it wires together: every method
// must be called from one controlling thread.
class IpcService {
public:
    // `event_bus`, `session_manager`, and `configuration_manager` must
    // outlive this service.
    IpcService(core::EventBus& event_bus, session::SessionManager& session_manager,
               config::ConfigurationManager& configuration_manager,
               std::filesystem::path socket_path = default_socket_path(),
               std::filesystem::path models_directory = speech::default_models_path());

    IpcService(const IpcService&) = delete;
    IpcService& operator=(const IpcService&) = delete;
    IpcService(IpcService&&) = delete;
    IpcService& operator=(IpcService&&) = delete;

    // Starts listening. Returns an error, without side effects, if the
    // socket cannot be created.
    std::optional<IpcError> start();

    // Closes every client connection and the listening socket, and
    // removes the socket file. Safe to call when not running.
    void stop();

    // Services the socket for up to `timeout_ms`: accepts connections,
    // routes each complete line to subscribe_events/unsubscribe_events
    // or the CommandDispatcher, sends back the resulting response, and
    // forgets a client's event subscription when it disconnects. Meant
    // to be called repeatedly from the composition root's run loop.
    void poll_once(int timeout_ms);

private:
    IpcServer server_;
    CommandDispatcher dispatcher_;
    EventBridge event_bridge_;
};

} // namespace yoru::ipc
