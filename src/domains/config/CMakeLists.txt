# Configuration domain: user preferences and their defaults.
# Header-only for now — no behavior to compile yet.
add_library(yoru-domain-config INTERFACE)
target_include_directories(yoru-domain-config INTERFACE ${PROJECT_SOURCE_DIR}/src)
