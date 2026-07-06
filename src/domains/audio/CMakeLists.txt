# Audio domain: microphone capture. miniaudio is encapsulated entirely in
# capture_device.cpp; no other translation unit includes <miniaudio.h>.
find_package(PkgConfig REQUIRED)
pkg_check_modules(miniaudio REQUIRED IMPORTED_TARGET miniaudio)

add_library(yoru-domain-audio capture_device.cpp recording.cpp recording_manager.cpp)

target_include_directories(yoru-domain-audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-audio
    PUBLIC
        yoru-core
        yoru-domain-session
    PRIVATE
        PkgConfig::miniaudio
        yoru-warnings
)
