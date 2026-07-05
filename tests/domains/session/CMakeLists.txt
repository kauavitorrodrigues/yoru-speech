add_executable(
    yoru-domain-session-tests
    test_service_state.cpp
    test_session.cpp
    test_events.cpp
)

target_link_libraries(
    yoru-domain-session-tests
    PRIVATE
        yoru-domain-session
        yoru-core
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-session-tests)
