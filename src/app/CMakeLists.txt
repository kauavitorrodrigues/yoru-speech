# Service executable and composition root.
add_executable(yoru-speech main.cpp)
target_link_libraries(yoru-speech PRIVATE yoru-core yoru-warnings)
