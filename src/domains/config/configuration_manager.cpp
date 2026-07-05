#include "domains/config/configuration_manager.hpp"

#include "domains/config/events.hpp"

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace yoru::config {

namespace {

std::filesystem::path home_directory() {
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home);
    }
    return {};
}

// Reads `path` into `out_contents`. Returns an error message on failure,
// nullopt on success. Never throws: a missing or unreadable file is a
// normal, expected condition, not an exceptional one.
std::optional<std::string> read_file(const std::filesystem::path& path, std::string& out_contents) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return "failed to open file for reading";
    }

    std::ostringstream contents;
    contents << stream.rdbuf();
    if (stream.bad()) {
        return "failed to read file contents";
    }

    out_contents = contents.str();
    return std::nullopt;
}

// Writes `contents` via a temporary file and renames it into place, so
// readers never observe a partial write (rename() is atomic with respect
// to concurrent opens on the same filesystem). This does not guarantee
// durability across a power loss: that would additionally require
// fsync-ing the temp file and its containing directory, which is an
// acceptable trade-off for a user preferences file. The temp file name
// includes the process id so two instances writing concurrently never
// collide.
// Returns an error message on failure, nullopt on success. Never throws.
std::optional<std::string> write_file_atomically(const std::filesystem::path& path,
                                                 const std::string& contents) {
    std::filesystem::path temp_path = path;
    temp_path += ".tmp." + std::to_string(::getpid());

    {
        std::ofstream stream(temp_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return "failed to open temporary file for writing";
        }
        stream << contents;
        stream.flush();
        if (!stream) {
            return "failed to write temporary file";
        }
    }

    std::error_code error_code;
    std::filesystem::rename(temp_path, path, error_code);
    if (error_code) {
        std::error_code ignored;
        std::filesystem::remove(temp_path, ignored);
        return "failed to move temporary file into place: " + error_code.message();
    }
    return std::nullopt;
}

} // namespace

std::filesystem::path default_config_path() {
    std::filesystem::path base;
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        base = xdg_config_home;
    } else {
        // Falls back to a relative ".config" if HOME is also unset. This is
        // an unlikely environment for a user service, but never throws.
        base = home_directory() / ".config";
    }
    return base / "yoru-speech" / "config.toml";
}

ConfigurationManager::ConfigurationManager(std::filesystem::path config_path,
                                           core::EventBus& event_bus)
    : config_path_(std::move(config_path)), event_bus_(event_bus) {}

std::vector<ValidationError> ConfigurationManager::load() {
    std::error_code error_code;
    std::filesystem::create_directories(config_path_.parent_path(), error_code);
    if (error_code) {
        configuration_ = Configuration{};
        return {ValidationError{"<file>",
                                "failed to create config directory: " + error_code.message()}};
    }

    if (!std::filesystem::exists(config_path_)) {
        configuration_ = Configuration{};
        if (auto error = write_file_atomically(config_path_, encode_toml(configuration_))) {
            return {ValidationError{"<file>", *error}};
        }
        return {};
    }

    std::string contents;
    if (auto error = read_file(config_path_, contents)) {
        configuration_ = Configuration{};
        return {ValidationError{"<file>", *error}};
    }

    const DecodedConfiguration decoded = decode_toml(contents, Configuration{});
    configuration_ = decoded.configuration;
    return decoded.errors;
}

const Configuration& ConfigurationManager::current() const {
    return configuration_;
}

std::vector<ValidationError> ConfigurationManager::update(Configuration next) {
    std::vector<ValidationError> errors = validate(next);
    if (!errors.empty()) {
        return errors;
    }

    if (auto error = write_file_atomically(config_path_, encode_toml(next))) {
        return {ValidationError{"<file>", *error}};
    }

    configuration_ = std::move(next);
    event_bus_.publish(ConfigurationChanged{configuration_});
    return {};
}

} // namespace yoru::config
