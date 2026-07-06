# Core: cross-cutting service infrastructure (logging, event distribution,
# session_id.hpp, error_event.hpp). Depends on nothing but the standard
# library. Every domain depends on core, never the other way around.
#
# Domain event payload types (RecordingFinished, TranscriptionCompleted,
# etc.) are owned and defined by the domains that publish them, not by
# core. SessionId and ErrorOccurred are the deliberate exception: they
# carry no domain-specific fields and are used by multiple domains that
# the Session Manager (domains/session) itself depends on, so keeping them
# in any one domain would create a dependency cycle.
add_library(yoru-core logger.cpp)

target_include_directories(yoru-core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(yoru-core PRIVATE yoru-warnings)
