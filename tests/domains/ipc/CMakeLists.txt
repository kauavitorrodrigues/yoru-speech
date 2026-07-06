add_executable(yoru-domain-ipc-tests test_ipc_server.cpp)

target_link_libraries(
    yoru-domain-ipc-tests
    PRIVATE
        yoru-domain-ipc
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-ipc-tests)
