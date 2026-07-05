#include "logger.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <ostream>

namespace yoru::core {

namespace {

// Minimum level shared across the whole process. Atomic because loggers from
// different components may read it concurrently.
std::atomic<LogLevel> g_min_level{LogLevel::Info};

// Serializes output so messages from distinct threads do not interleave
// within a single line.
std::mutex g_output_mutex;

std::string_view level_label(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

constexpr const char* kTimestampFormat = "%Y-%m-%d %H:%M:%S";

// Size of the rendered timestamp: "YYYY-MM-DD HH:MM:SS" is 19 chars plus the
// null terminator. Kept next to the format string so both stay in sync.
constexpr std::size_t kTimestampBufferSize = 20;

std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
    localtime_r(&time, &local);

    char buffer[kTimestampBufferSize];
    if (std::strftime(buffer, sizeof(buffer), kTimestampFormat, &local) == 0) {
        return {};
    }
    return buffer;
}

} // namespace

Logger::Logger(std::string component) : component_(std::move(component)) {}

void Logger::debug(std::string_view message) const {
    log(LogLevel::Debug, message);
}

void Logger::info(std::string_view message) const {
    log(LogLevel::Info, message);
}

void Logger::warn(std::string_view message) const {
    log(LogLevel::Warn, message);
}

void Logger::error(std::string_view message) const {
    log(LogLevel::Error, message);
}

void Logger::set_level(LogLevel level) {
    g_min_level.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() {
    return g_min_level.load(std::memory_order_relaxed);
}

void Logger::log(LogLevel level, std::string_view message) const {
    if (level < g_min_level.load(std::memory_order_relaxed)) {
        return;
    }

    const std::string timestamp = current_timestamp();

    const std::lock_guard<std::mutex> guard(g_output_mutex);
    std::clog << '[' << timestamp << "] [" << level_label(level) << "] [" << component_ << "] "
              << message << '\n';

    // Warn and Error usually precede failures: make sure the message reaches
    // stderr even on abnormal termination, at the cost of an extra flush.
    if (level >= LogLevel::Warn) {
        std::clog.flush();
    }
}

} // namespace yoru::core
