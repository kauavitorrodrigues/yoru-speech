#include "domains/clipboard/wl_clipboard_adapter.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <thread>

namespace yoru::clipboard {

namespace {

// How often the parent checks whether the child has exited yet, while
// waiting for it within the configured timeout.
constexpr std::chrono::milliseconds kPollInterval{10};

std::string errno_message(const char* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

// Redirects the calling (child) process's stdout and stderr to
// /dev/null. Whatever the clipboard command writes there is not this
// service's output: left inherited, it would interleave with (or
// flood) the daemon's own logging, which goes to stderr.
void silence_stdio() {
    const int dev_null = ::open("/dev/null", O_WRONLY);
    if (dev_null == -1) {
        return;
    }
    ::dup2(dev_null, STDOUT_FILENO);
    ::dup2(dev_null, STDERR_FILENO);
    ::close(dev_null);
}

} // namespace

WlClipboardAdapter::WlClipboardAdapter(std::string command, std::chrono::milliseconds timeout)
    : command_(std::move(command)), timeout_(timeout) {}

std::optional<ClipboardError> WlClipboardAdapter::copy(const std::string& text) const {
    int stdin_pipe[2];
    if (::pipe(stdin_pipe) == -1) {
        return ClipboardError{errno_message("failed to create pipe")};
    }

    const pid_t child_pid = ::fork();
    if (child_pid == -1) {
        const auto error = errno_message("failed to fork");
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        return ClipboardError{error};
    }

    if (child_pid == 0) {
        // Child: wire the pipe's read end to stdin, silence stdout/
        // stderr, then replace this process with the clipboard command.
        // execlp() only returns on failure; _exit() (not exit()) avoids
        // re-running any of the parent's atexit handlers or destructors
        // of copied statics.
        ::dup2(stdin_pipe[0], STDIN_FILENO);
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        silence_stdio();
        ::execlp(command_.c_str(), command_.c_str(), static_cast<char*>(nullptr));
        ::_exit(127); // Conventional "command not found" exit code.
    }

    // Parent: hand the text to the child, then wait for it, bounded by
    // timeout_.
    ::close(stdin_pipe[0]);
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t sent = ::write(stdin_pipe[1], text.data() + written, text.size() - written);
        if (sent <= 0) {
            break; // The child died or closed its end; nothing more to do.
        }
        written += static_cast<std::size_t>(sent);
    }
    ::close(stdin_pipe[1]);

    const auto deadline = std::chrono::steady_clock::now() + timeout_;
    int status = 0;
    for (;;) {
        const pid_t result = ::waitpid(child_pid, &status, WNOHANG);
        if (result == child_pid) {
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(child_pid, SIGKILL);
            ::waitpid(child_pid, &status, 0);
            return ClipboardError{command_ + " timed out"};
        }
        std::this_thread::sleep_for(kPollInterval);
    }

    if (!WIFEXITED(status)) {
        return ClipboardError{command_ + " terminated abnormally"};
    }

    const int exit_code = WEXITSTATUS(status);
    if (exit_code == 127) {
        return ClipboardError{command_ + " not found"};
    }
    if (exit_code != 0) {
        return ClipboardError{command_ + " exited with status " + std::to_string(exit_code)};
    }

    return std::nullopt;
}

} // namespace yoru::clipboard
