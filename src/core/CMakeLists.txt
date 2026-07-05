# Core: cross-cutting service infrastructure (logging, event distribution).
# Depends on nothing but the standard library. Every domain depends on
# core, never the other way around. Domain event payload types are owned
# and defined by the domains that publish them, not by core.
add_library(yoru-core logger.cpp)

target_include_directories(yoru-core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(yoru-core PRIVATE yoru-warnings)
