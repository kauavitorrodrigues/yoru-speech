add_executable(
    yoru-domain-config-tests
    test_configuration.cpp
    test_events.cpp
    test_toml_codec.cpp
    test_configuration_manager.cpp
)

target_link_libraries(
    yoru-domain-config-tests
    PRIVATE
        yoru-domain-config
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-config-tests)
