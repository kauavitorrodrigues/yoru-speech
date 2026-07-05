#include "domains/config/configuration_manager.hpp"

#include "core/event_bus.hpp"
#include "domains/config/events.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <atomic>
#include <fstream>

using yoru::config::Configuration;
using yoru::config::ConfigurationChanged;
using yoru::config::ConfigurationManager;
using yoru::config::ModelLoadPolicy;
using yoru::core::EventBus;

namespace {

// Each test gets its own directory, unique across processes (pid) and
// within a process (counter). The destructor removes it unconditionally,
// including when a REQUIRE aborts the test case mid-way, so a failed
// assertion can never leak state into a later test run.
class TempConfigDir {
public:
    TempConfigDir() : path_(make_unique_dir()) {}

    ~TempConfigDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempConfigDir(const TempConfigDir&) = delete;
    TempConfigDir& operator=(const TempConfigDir&) = delete;

    std::filesystem::path config_path() const {
        return path_ / "config.toml";
    }

private:
    static std::filesystem::path make_unique_dir() {
        static std::atomic<int> counter{0};
        return std::filesystem::temp_directory_path() /
               ("yoru-speech-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    }

    std::filesystem::path path_;
};

} // namespace

TEST_CASE("load() creates a default config file when none exists") {
    const TempConfigDir dir;
    REQUIRE_FALSE(std::filesystem::exists(dir.config_path()));

    EventBus bus;
    ConfigurationManager manager(dir.config_path(), bus);
    const auto errors = manager.load();

    CHECK(errors.empty());
    CHECK(std::filesystem::exists(dir.config_path()));
    CHECK(manager.current().default_language == "auto");
}

TEST_CASE("load() reads a valid existing file") {
    const TempConfigDir dir;
    std::filesystem::create_directories(dir.config_path().parent_path());
    {
        std::ofstream out(dir.config_path());
        out << "default_language = \"pt\"\n"
            << "selected_model = \"small\"\n"
            << "auto_clipboard = false\n"
            << "model_load_policy = \"always_loaded\"\n";
    }

    EventBus bus;
    ConfigurationManager manager(dir.config_path(), bus);
    const auto errors = manager.load();

    CHECK(errors.empty());
    CHECK(manager.current().default_language == "pt");
    CHECK(manager.current().selected_model == "small");
    CHECK(manager.current().auto_clipboard == false);
    CHECK(manager.current().model_load_policy == ModelLoadPolicy::AlwaysLoaded);
}

TEST_CASE("load() merges a partial file with defaults") {
    const TempConfigDir dir;
    std::filesystem::create_directories(dir.config_path().parent_path());
    {
        std::ofstream out(dir.config_path());
        out << "default_language = \"pt\"\n";
    }

    EventBus bus;
    ConfigurationManager manager(dir.config_path(), bus);
    const auto errors = manager.load();

    CHECK(errors.empty());
    CHECK(manager.current().default_language == "pt");
    CHECK(manager.current().selected_model == "base");
    CHECK(manager.current().auto_clipboard == true);
}

TEST_CASE("load() falls back to defaults for invalid fields and reports errors") {
    const TempConfigDir dir;
    std::filesystem::create_directories(dir.config_path().parent_path());
    {
        std::ofstream out(dir.config_path());
        out << "default_language = \"portuguese\"\n"
            << "selected_model = \"\"\n";
    }

    EventBus bus;
    ConfigurationManager manager(dir.config_path(), bus);
    const auto errors = manager.load();

    CHECK(errors.size() == 2);
    CHECK(manager.current().default_language == "auto");
    CHECK(manager.current().selected_model == "base");
}

TEST_CASE("update() persists changes, notifies subscribers, and reloads correctly") {
    const TempConfigDir dir;

    EventBus bus;
    int change_notifications = 0;
    Configuration received;
    bus.subscribe<ConfigurationChanged>([&](const ConfigurationChanged& event) {
        ++change_notifications;
        received = event.configuration;
    });

    ConfigurationManager manager(dir.config_path(), bus);
    manager.load();

    Configuration updated = manager.current();
    updated.default_language = "pt";
    updated.auto_clipboard = false;

    const auto errors = manager.update(updated);
    CHECK(errors.empty());
    CHECK(change_notifications == 1);
    CHECK(received.default_language == "pt");
    CHECK(received.auto_clipboard == false);

    ConfigurationManager reloaded(dir.config_path(), bus);
    reloaded.load();
    CHECK(reloaded.current().default_language == "pt");
    CHECK(reloaded.current().auto_clipboard == false);
}

TEST_CASE("update() rejects an invalid configuration and leaves the current one untouched") {
    const TempConfigDir dir;

    EventBus bus;
    int change_notifications = 0;
    bus.subscribe<ConfigurationChanged>(
        [&](const ConfigurationChanged&) { ++change_notifications; });

    ConfigurationManager manager(dir.config_path(), bus);
    manager.load();
    const Configuration before = manager.current();

    Configuration invalid = before;
    invalid.default_language = "not-a-code";

    const auto errors = manager.update(invalid);

    CHECK_FALSE(errors.empty());
    CHECK(change_notifications == 0);
    CHECK(manager.current().default_language == before.default_language);
}
