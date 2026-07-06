add_executable(
    yoru-domain-speech-tests
    test_types.cpp
    test_events.cpp
    test_whisper_backend.cpp
)

target_link_libraries(
    yoru-domain-speech-tests
    PRIVATE
        yoru-domain-speech
        yoru-core
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-speech-tests)
