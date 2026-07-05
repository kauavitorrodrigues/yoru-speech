#pragma once

#include <string>
#include <string_view>

namespace yoru::core {

// Severity levels, ordered from least to most critical.
// Order matters: the global filter discards messages below the active level.
enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

// Operational event logging for the service.
//
// Each Logger carries the name of the component using it, so every message
// identifies its origin without the caller having to repeat the name.
// Output is written to stderr, reserving stdout for future service use.
//
// The Logger never changes system behavior: write failures are ignored
// silently so it introduces no new failure modes.
class Logger {
public:
    explicit Logger(std::string component);

    void debug(std::string_view message) const;
    void info(std::string_view message) const;
    void warn(std::string_view message) const;
    void error(std::string_view message) const;

    // Sets the global minimum level. Messages below it are discarded.
    // Shared by every logger in the process.
    static void set_level(LogLevel level);
    static LogLevel level();

private:
    void log(LogLevel level, std::string_view message) const;

    std::string component_;
};

} // namespace yoru::core
