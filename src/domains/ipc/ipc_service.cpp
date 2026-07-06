#include "domains/ipc/ipc_service.hpp"

#include "domains/ipc/message_codec.hpp"

namespace yoru::ipc {

namespace {

constexpr const char* kSubscribeEvents = "subscribe_events";
constexpr const char* kUnsubscribeEvents = "unsubscribe_events";

} // namespace

IpcService::IpcService(core::EventBus& event_bus, session::SessionManager& session_manager,
                       config::ConfigurationManager& configuration_manager,
                       std::filesystem::path socket_path, std::filesystem::path models_directory)
    : server_(std::move(socket_path)),
      dispatcher_(session_manager, configuration_manager, std::move(models_directory)),
      event_bridge_(event_bus, server_) {}

std::optional<IpcError> IpcService::start() {
    return server_.start();
}

void IpcService::stop() {
    server_.stop();
}

void IpcService::poll_once(int timeout_ms) {
    server_.poll_once(
        timeout_ms,
        [this](IpcServer::ClientId client_id, const std::string& line) {
            const ParsedMessage message = parse_message(line);

            if (message.type == kSubscribeEvents) {
                event_bridge_.subscribe(client_id);
                server_.send_line(client_id, encode_ack(kSubscribeEvents));
                return;
            }
            if (message.type == kUnsubscribeEvents) {
                event_bridge_.unsubscribe(client_id);
                server_.send_line(client_id, encode_ack(kUnsubscribeEvents));
                return;
            }

            server_.send_line(client_id, dispatcher_.handle_line(line));
        },
        [this](IpcServer::ClientId client_id) { event_bridge_.unsubscribe(client_id); });
}

} // namespace yoru::ipc
