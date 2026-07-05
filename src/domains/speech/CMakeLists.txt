# Speech domain: transcription results and recognition model metadata.
# Header-only for now — no behavior to compile yet.
add_library(yoru-domain-speech INTERFACE)
target_include_directories(yoru-domain-speech INTERFACE ${PROJECT_SOURCE_DIR}/src)
