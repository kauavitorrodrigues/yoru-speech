# Core: cross-cutting service infrastructure (logging, event distribution).
# Depends on domain types to define event payloads (Infrastructure ->
# Domain, per docs/runtime-architecture.md); domains never depend back on
# core.
add_library(yoru-core logger.cpp)

target_include_directories(yoru-core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-core
    PUBLIC
        yoru-domain-session
        yoru-domain-speech
        yoru-domain-config
    PRIVATE
        yoru-warnings
)
