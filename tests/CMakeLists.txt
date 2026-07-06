find_package(doctest REQUIRED)
include(doctest)

# Provides doctest's implementation and default main() in a single
# translation unit, shared by every test executable below.
add_library(yoru-test-main STATIC test_main.cpp)
target_link_libraries(yoru-test-main PUBLIC doctest::doctest)

add_subdirectory(domains/session)
add_subdirectory(domains/speech)
add_subdirectory(domains/config)
add_subdirectory(domains/audio)
add_subdirectory(domains/ipc)
add_subdirectory(domains/clipboard)
add_subdirectory(core)
