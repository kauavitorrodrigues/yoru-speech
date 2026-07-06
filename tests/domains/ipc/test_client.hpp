#pragma once

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace yoru::ipc::test {

// A raw Unix Domain Socket client used only to drive an IpcServer (or
// anything built on top of it) from the other end in tests, the same
// role a real IPC client (a Python script, socat) plays in production.
// Test-only: not part of the production ipc domain.
class TestClient {
public:
    explicit TestClient(const std::filesystem::path& socket_path) {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(fd_ != -1);

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const std::string path_string = socket_path.string();
        std::strncpy(address.sun_path, path_string.c_str(), sizeof(address.sun_path) - 1);

        REQUIRE(::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    }

    ~TestClient() {
        if (fd_ != -1) {
            ::close(fd_);
        }
    }

    TestClient(const TestClient&) = delete;
    TestClient& operator=(const TestClient&) = delete;

    void send_line(const std::string& line) const {
        const std::string data = line + "\n";
        REQUIRE(::write(fd_, data.data(), data.size()) == static_cast<ssize_t>(data.size()));
    }

    void send_raw(const std::string& data) const {
        std::size_t written = 0;
        while (written < data.size()) {
            const ssize_t sent = ::write(fd_, data.data() + written, data.size() - written);
            REQUIRE(sent > 0);
            written += static_cast<std::size_t>(sent);
        }
    }

    void close_connection() {
        ::close(fd_);
        fd_ = -1;
    }

    // Returns the next complete line received (without the newline),
    // waiting for at most `attempts` short polls if one isn't already
    // buffered. Empty if nothing arrived in time. Lines beyond the first
    // one read from a single socket chunk are kept in read_buffer_ for
    // the next call, so a sequence of read_line() calls correctly
    // drains multiple lines delivered in one burst (e.g. an ack
    // immediately followed by a pushed event).
    std::string read_line(int attempts = 20) {
        for (int i = 0; i < attempts; ++i) {
            const auto newline_pos = read_buffer_.find('\n');
            if (newline_pos != std::string::npos) {
                const std::string line = read_buffer_.substr(0, newline_pos);
                read_buffer_.erase(0, newline_pos + 1);
                return line;
            }

            pollfd poll_fd{fd_, POLLIN, 0};
            if (::poll(&poll_fd, 1, 50) > 0 && (poll_fd.revents & POLLIN) != 0) {
                char chunk[512] = {};
                const ssize_t bytes_read = ::read(fd_, chunk, sizeof(chunk));
                if (bytes_read > 0) {
                    read_buffer_.append(chunk, static_cast<std::size_t>(bytes_read));
                }
            }
        }

        const std::string leftover = read_buffer_;
        read_buffer_.clear();
        return leftover;
    }

private:
    int fd_ = -1;
    std::string read_buffer_;
};

inline std::filesystem::path unique_test_socket_path(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           (name + "-" + std::to_string(getpid()) + ".sock");
}

} // namespace yoru::ipc::test
