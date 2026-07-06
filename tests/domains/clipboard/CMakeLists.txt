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

# The real system clipboard is a single, global, cross-process resource:
# tests that touch it via real wl-copy/wl-paste must never run
# concurrently with each other or with other binaries' clipboard tests
# (see tests/domains/ipc/CMakeLists.txt), or a `ctest -j` run can flake on
# a race for clipboard ownership.
doctest_discover_tests(yoru-domain-clipboard-tests PROPERTIES RESOURCE_LOCK wayland_clipboard)
