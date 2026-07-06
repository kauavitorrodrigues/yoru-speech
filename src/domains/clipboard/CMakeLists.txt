# Clipboard domain: copies transcribed text to the system clipboard.
# wl_clipboard_adapter.cpp is the only translation unit that invokes
# wl-copy; nothing else in the project knows it exists.
add_library(yoru-domain-clipboard wl_clipboard_adapter.cpp auto_clipboard.cpp)

target_include_directories(yoru-domain-clipboard PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-clipboard
    PUBLIC
        yoru-core
        yoru-domain-config
        yoru-domain-speech
    PRIVATE
        yoru-warnings
)
