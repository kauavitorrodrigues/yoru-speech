add_executable(yoru-domain-audio-tests test_capture_device.cpp)

target_link_libraries(
    yoru-domain-audio-tests
    PRIVATE
        yoru-domain-audio
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-audio-tests)
