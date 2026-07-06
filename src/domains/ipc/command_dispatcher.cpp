#include "domains/ipc/command_dispatcher.hpp"

#include "domains/ipc/message_codec.hpp"

#include <cstddef>
#include <variant>

namespace yoru::ipc {

namespace {

constexpr const char* kStartRecording = "start_recording";
constexpr const char* kStopRecording = "stop_recording";
constexpr const char* kCancelSession = "cancel_session";
constexpr const char* kGetState = "get_state";
constexpr const char* kGetConfig = "get_config";
constexpr const char* kSetConfig = "set_config";
constexpr const char* kListModels = "list_models";

} // namespace

CommandDispatcher::CommandDispatcher(session::SessionManager& session_manager,
                                     config::ConfigurationManager& configuration_manager,
                                     std::filesystem::path models_directory)
    : session_manager_(session_manager), configuration_manager_(configuration_manager),
      models_directory_(std::move(models_directory)) {}

std::string CommandDispatcher::handle_line(const std::string& line) const {
    const ParsedMessage message = parse_message(line);
    if (!message.type.has_value()) {
        return encode_error("error", message.error.value_or("malformed message"));
    }
    const std::string& type = message.type.value();

    if (type == kStartRecording) {
        if (const auto error = session_manager_.start_session()) {
            return encode_error(type, error->message);
        }
        return encode_ack(type);
    }

    if (type == kStopRecording) {
        auto result = session_manager_.stop_session();
        if (const auto* error = std::get_if<session::SessionError>(&result)) {
            return encode_error(type, error->message);
        }
        return encode_transcript(type, std::get<speech::Transcript>(result));
    }

    if (type == kCancelSession) {
        if (const auto error = session_manager_.cancel_session()) {
            return encode_error(type, error->message);
        }
        return encode_ack(type);
    }

    if (type == kGetState) {
        return encode_state(type, session_manager_.state());
    }

    if (type == kGetConfig) {
        return encode_config(type, configuration_manager_.current());
    }

    if (type == kSetConfig) {
        const DecodedSetConfig decoded = decode_set_config(line, configuration_manager_.current());
        if (decoded.error.has_value()) {
            return encode_error(type, decoded.error.value());
        }

        const auto validation_errors = configuration_manager_.update(decoded.configuration);
        if (!validation_errors.empty()) {
            std::string message_text = "invalid configuration:";
            for (std::size_t i = 0; i < validation_errors.size(); ++i) {
                const auto& validation_error = validation_errors[i];
                message_text += (i == 0 ? " " : ", ") + validation_error.field + ": " +
                                validation_error.message;
            }
            return encode_error(type, message_text);
        }

        return encode_config(type, configuration_manager_.current());
    }

    if (type == kListModels) {
        return encode_models(type, speech::list_available_models(models_directory_));
    }

    return encode_error(type, "unknown command: " + type);
}

} // namespace yoru::ipc
