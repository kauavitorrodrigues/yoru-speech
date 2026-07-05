cmake_minimum_required(VERSION 3.20)

project(
    yoru-speech
    VERSION 0.0.0
    DESCRIPTION "Offline voice service for Linux, part of the Yoru ecosystem"
    LANGUAGES CXX
)

# --- Language standard (ADR-015.2) ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Place the final executable at the root of the build directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Default build type for local development when none is specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Compilation flags ---
# Applied to every project target through an interface library, without
# polluting the global CMAKE_CXX_FLAGS (which would also affect dependencies).
add_library(yoru-warnings INTERFACE)
target_compile_options(
    yoru-warnings
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
)

add_subdirectory(src)

# --- Tests ---
option(YORU_BUILD_TESTS "Build unit tests" ON)
if(YORU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
