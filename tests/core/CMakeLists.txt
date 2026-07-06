add_executable(yoru-core-tests test_event_bus.cpp test_error_event.cpp)

target_link_libraries(
    yoru-core-tests
    PRIVATE
        yoru-core
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-core-tests)
