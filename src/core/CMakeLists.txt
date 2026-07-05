# Core: cross-cutting service infrastructure (logging and, later, fundamental
# state types and process-wide primitives).
add_library(yoru-core logger.cpp)

target_include_directories(yoru-core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(yoru-core PRIVATE yoru-warnings)
