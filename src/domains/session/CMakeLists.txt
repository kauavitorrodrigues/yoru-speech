# Session domain: coordinates the lifecycle of a dictation session and the
# service-wide state derived from it. The Session Manager depends on the
# audio and speech domains to orchestrate them; neither depends back on
# session (see core/session_id.hpp for why that would otherwise cycle).
# Also depends on config, to seed and track the language/prompt passed to
# the Speech Backend (config has no dependencies of its own, so this
# cannot cycle either).
add_library(yoru-domain-session service_state.cpp session.cpp session_manager.cpp)

target_include_directories(yoru-domain-session PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-session
    PUBLIC
        yoru-core
        yoru-domain-audio
        yoru-domain-speech
        yoru-domain-config
    PRIVATE
        yoru-warnings
)
