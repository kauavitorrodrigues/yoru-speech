# Service executable and composition root.
add_executable(yoru-speech main.cpp)
target_link_libraries(
    yoru-speech
    PRIVATE
        yoru-core
        yoru-domain-audio
        yoru-domain-config
        yoru-domain-session
        yoru-domain-speech
        yoru-domain-ipc
        yoru-domain-clipboard
        yoru-warnings
)
