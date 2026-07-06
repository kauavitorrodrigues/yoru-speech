add_executable(
    yoru-domain-clipboard-tests
    test_wl_clipboard_adapter.cpp
    test_auto_clipboard.cpp
)

target_link_libraries(
    yoru-domain-clipboard-tests
    PRIVATE
        yoru-domain-clipboard
        yoru-core
        yoru-test-main
        yoru-warnings
)

doctest_discover_tests(yoru-domain-clipboard-tests)
