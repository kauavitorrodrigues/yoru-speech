# Session domain: coordinates the lifecycle of a dictation session and the
# service-wide state derived from it.
add_library(yoru-domain-session service_state.cpp session.cpp)

target_include_directories(yoru-domain-session PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-session
    PUBLIC
        yoru-core
    PRIVATE
        yoru-warnings
)
