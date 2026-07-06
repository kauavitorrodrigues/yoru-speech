#include "domains/speech/model_repository.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include <unistd.h>

using yoru::speech::find_model;
using yoru::speech::list_available_models;
using yoru::speech::Model;
using yoru::speech::ModelSize;

namespace {

void touch(const std::filesystem::path& path) {
    std::ofstream(path).put('\0');
}

// Suffixed with the process id so concurrent test binaries (e.g. ctest -j)
// never collide on the same directory under temp_directory_path().
std::filesystem::path unique_test_dir(const std::string& name) {
    return std::filesystem::temp_directory_path() / (name + "-" + std::to_string(getpid()));
}

} // namespace

TEST_CASE("list_available_models() on a nonexistent directory returns an empty list") {
    const auto models = list_available_models("/nonexistent/yoru-speech-models-dir");

    CHECK(models.empty());
}

TEST_CASE("list_available_models() on an empty directory returns an empty list") {
    const auto dir = unique_test_dir("yoru-speech-test-empty-models");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto models = list_available_models(dir);

    CHECK(models.empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("list_available_models() ignores files that are not .bin") {
    const auto dir = unique_test_dir("yoru-speech-test-non-bin");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    touch(dir / "ggml-tiny.bin");
    touch(dir / "README.md");
    touch(dir / "ggml-tiny.bin.sha256");

    const auto models = list_available_models(dir);

    REQUIRE(models.size() == 1);
    CHECK(models[0].name == "ggml-tiny");

    std::filesystem::remove_all(dir);
}

TEST_CASE("list_available_models() skips .bin files with no recognizable size token") {
    const auto dir = unique_test_dir("yoru-speech-test-unrecognized");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    // "database" must not be mistaken for a substring match on "base".
    touch(dir / "notes-database.bin");
    touch(dir / "ggml-tiny.bin");

    const auto models = list_available_models(dir);

    REQUIRE(models.size() == 1);
    CHECK(models[0].name == "ggml-tiny");

    std::filesystem::remove_all(dir);
}

TEST_CASE("list_available_models() derives size, language, and backend from the filename") {
    const auto dir = unique_test_dir("yoru-speech-test-metadata");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    touch(dir / "ggml-tiny.en-q5_1.bin");
    touch(dir / "ggml-large-v3.bin");

    auto models = list_available_models(dir);
    std::sort(models.begin(), models.end(),
              [](const Model& a, const Model& b) { return a.name < b.name; });

    REQUIRE(models.size() == 2);

    CHECK(models[0].name == "ggml-large-v3");
    CHECK(models[0].size == ModelSize::Large);
    CHECK(models[0].supported_language == "multi");
    CHECK(models[0].backend == "whisper.cpp");

    CHECK(models[1].name == "ggml-tiny.en-q5_1");
    CHECK(models[1].size == ModelSize::Tiny);
    CHECK(models[1].supported_language == "en");

    std::filesystem::remove_all(dir);
}

namespace {

Model make_model(std::string name, std::filesystem::path path) {
    return Model{
        .name = std::move(name),
        .size = ModelSize::Base,
        .supported_language = "multi",
        .path = std::move(path),
        .backend = "whisper.cpp",
    };
}

} // namespace

TEST_CASE("find_model() returns the model whose name matches") {
    const std::vector<Model> available{
        make_model("ggml-tiny", "/models/ggml-tiny.bin"),
        make_model("ggml-base", "/models/ggml-base.bin"),
    };

    const auto found = find_model(available, "ggml-base");

    REQUIRE(found.has_value());
    CHECK(found->path == "/models/ggml-base.bin");
}

TEST_CASE("find_model() returns nullopt when no model matches") {
    const std::vector<Model> available{
        make_model("ggml-tiny", "/models/ggml-tiny.bin"),
    };

    CHECK_FALSE(find_model(available, "ggml-large").has_value());
}
