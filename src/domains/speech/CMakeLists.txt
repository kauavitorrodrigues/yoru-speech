# Speech domain: transcription results, recognition model metadata, and
# the Speech Engine. whisper.cpp is encapsulated entirely in
# whisper_backend.cpp; no other translation unit includes <whisper.h>.
find_package(whisper REQUIRED)

add_library(yoru-domain-speech whisper_backend.cpp model_repository.cpp)

target_include_directories(yoru-domain-speech PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-speech
    PUBLIC
        yoru-core
        yoru-domain-session
    PRIVATE
        whisper
        yoru-warnings
)
