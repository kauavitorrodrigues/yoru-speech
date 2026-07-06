# IPC domain: the service's public interface over Unix Domain Sockets.
# ipc_server.cpp is pure POSIX socket syscalls, no external library.
add_library(yoru-domain-ipc ipc_server.cpp)

target_include_directories(yoru-domain-ipc PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(yoru-domain-ipc PRIVATE yoru-warnings)
