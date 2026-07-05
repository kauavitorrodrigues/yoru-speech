add_executable(yoru-domain-speech-tests test_types.cpp)

target_link_libraries(
    yoru-domain-speech-tests
    PRIVATE
        yoru-domain-speech
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-speech-tests)
