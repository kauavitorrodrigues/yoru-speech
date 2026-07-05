add_executable(yoru-domain-config-tests test_configuration.cpp)

target_link_libraries(
    yoru-domain-config-tests
    PRIVATE
        yoru-domain-config
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-config-tests)
