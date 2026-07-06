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
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-ipc-tests)
