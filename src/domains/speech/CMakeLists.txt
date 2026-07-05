# Speech domain: transcription results and recognition model metadata.
# Header-only for now, no behavior to compile yet. Depends on the session
# domain only for the SessionId value type used to tag its events.
add_library(yoru-domain-speech INTERFACE)
target_include_directories(yoru-domain-speech INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(yoru-domain-speech INTERFACE yoru-domain-session)
