# Configuration domain: user preferences, TOML persistence, and the
# Configuration Manager. toml++ is encapsulated entirely inside
# toml_codec.cpp; no other translation unit includes it.
find_package(tomlplusplus REQUIRED)

add_library(yoru-domain-config toml_codec.cpp configuration_manager.cpp)

target_include_directories(yoru-domain-config PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
    yoru-domain-config
    PUBLIC
        yoru-core
    PRIVATE
        tomlplusplus::tomlplusplus
        yoru-warnings
)
