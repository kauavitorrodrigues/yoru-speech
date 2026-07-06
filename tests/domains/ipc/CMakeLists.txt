add_executable(
    yoru-domain-ipc-tests
    test_ipc_server.cpp
    test_message_codec.cpp
    test_command_dispatcher.cpp
    test_event_bridge.cpp
    test_ipc_service.cpp
)

target_link_libraries(
    yoru-domain-ipc-tests
    PRIVATE
        yoru-domain-ipc
        yoru-domain-clipboard
        yoru-test-main
        yoru-warnings
)

# Two of this binary's tests drive a real AutoClipboard (real wl-copy/
# wl-paste) via the full IPC pipeline; the same RESOURCE_LOCK as
# tests/domains/clipboard/CMakeLists.txt keeps them from racing those
# tests, or each other, for the global system clipboard under `ctest -j`.
doctest_discover_tests(yoru-domain-ipc-tests PROPERTIES RESOURCE_LOCK wayland_clipboard)
