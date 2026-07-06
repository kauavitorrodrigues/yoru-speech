# IPC domain: the service's public interface over Unix Domain Sockets.
# ipc_server.cpp is pure POSIX socket syscalls, no external library.
# message_codec.cpp is the only translation unit that includes
# <nlohmann/json.hpp>. command_dispatcher.cpp translates messages into
# calls on the Session and Configuration Managers; event_bridge.cpp
# pushes EventBus facts to subscribed clients. Neither implements any
# domain logic of its own. ipc_service.cpp wires all three together into
# the one class the composition root needs.
find_package(nlohmann_json REQUIRED)

add_library(
    yoru-domain-ipc
    ipc_server.cpp
    message_codec.cpp
    command_dispatcher.cpp
    event_bridge.cpp
    ipc_service.cpp
)

target_include_directories(yoru-domain-ipc PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-ipc
    PUBLIC
        yoru-domain-session
        yoru-domain-config
        yoru-domain-speech
        yoru-domain-audio
    PRIVATE
        nlohmann_json::nlohmann_json
        yoru-warnings
)
