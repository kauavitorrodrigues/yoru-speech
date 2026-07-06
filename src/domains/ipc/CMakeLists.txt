# IPC domain: the service's public interface over Unix Domain Sockets.
# ipc_server.cpp is pure POSIX socket syscalls, no external library.
# message_codec.cpp is the only translation unit that includes
# <nlohmann/json.hpp>. command_dispatcher.cpp translates messages into
# calls on the Session and Configuration Managers; it implements no
# domain logic of its own.
find_package(nlohmann_json REQUIRED)

add_library(yoru-domain-ipc ipc_server.cpp message_codec.cpp command_dispatcher.cpp)

target_include_directories(yoru-domain-ipc PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-ipc
    PUBLIC
        yoru-domain-session
        yoru-domain-config
        yoru-domain-speech
    PRIVATE
        nlohmann_json::nlohmann_json
        yoru-warnings
)
