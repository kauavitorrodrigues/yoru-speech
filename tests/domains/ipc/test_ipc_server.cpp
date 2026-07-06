#include "domains/ipc/ipc_server.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using yoru::ipc::IpcServer;

namespace {

// A raw Unix Domain Socket client used only to drive the server under
// test from the other end, the same role a real IPC client (a Python
// script, socat) plays in production.
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

    // Reads until a newline arrives (or `attempts` short waits pass),
    // returning the line without it. Empty if nothing arrived in time.
    std::string read_line(int attempts = 20) const {
        std::string buffer;
        for (int i = 0; i < attempts; ++i) {
            pollfd poll_fd{fd_, POLLIN, 0};
            if (::poll(&poll_fd, 1, 50) > 0 && (poll_fd.revents & POLLIN) != 0) {
                char chunk[256] = {};
                const ssize_t bytes_read = ::read(fd_, chunk, sizeof(chunk));
                if (bytes_read > 0) {
                    buffer.append(chunk, static_cast<std::size_t>(bytes_read));
                }
            }
            const auto newline_pos = buffer.find('\n');
            if (newline_pos != std::string::npos) {
                return buffer.substr(0, newline_pos);
            }
        }
        return buffer;
    }

private:
    int fd_ = -1;
};

std::filesystem::path unique_test_socket_path() {
    return std::filesystem::temp_directory_path() /
           ("yoru-speech-test-" + std::to_string(getpid()) + ".sock");
}

} // namespace

TEST_CASE("start() creates a socket file, stop() removes it") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);

    REQUIRE_FALSE(server.start().has_value());
    CHECK(server.is_running());
    CHECK(std::filesystem::exists(path));

    server.stop();
    CHECK_FALSE(server.is_running());
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("start() removes a stale socket file left by a previous run") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    { std::ofstream(path).put('\0'); }
    REQUIRE(std::filesystem::exists(path));

    IpcServer server(path);
    const auto error = server.start();

    REQUIRE_FALSE(error.has_value());
    CHECK(server.is_running());

    server.stop();
}

TEST_CASE("start() twice reports an error without disrupting the running server") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    const auto second_error = server.start();

    REQUIRE(second_error.has_value());
    CHECK(server.is_running());

    server.stop();
}

TEST_CASE("a connected client's line is delivered via on_line") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    TestClient client(path);
    client.send_line(R"({"type":"start_recording"})");

    std::vector<std::string> received_lines;
    // A few short polls give the OS time to actually deliver the bytes
    // written above; a single immediate poll_once(0, ...) could race the
    // write.
    for (int i = 0; i < 20 && received_lines.empty(); ++i) {
        server.poll_once(
            50,
            [&](IpcServer::ClientId, const std::string& line) { received_lines.push_back(line); },
            [](IpcServer::ClientId) {});
    }

    REQUIRE(received_lines.size() == 1);
    CHECK(received_lines[0] == R"({"type":"start_recording"})");

    server.stop();
}

TEST_CASE("a partial line without a trailing newline is not delivered until completed") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    TestClient client(path);
    client.send_raw(R"({"type":)");

    std::vector<std::string> received_lines;
    for (int i = 0; i < 5; ++i) {
        server.poll_once(
            20,
            [&](IpcServer::ClientId, const std::string& line) { received_lines.push_back(line); },
            [](IpcServer::ClientId) {});
    }
    CHECK(received_lines.empty());

    client.send_raw("\"start_recording\"}\n");
    for (int i = 0; i < 20 && received_lines.empty(); ++i) {
        server.poll_once(
            50,
            [&](IpcServer::ClientId, const std::string& line) { received_lines.push_back(line); },
            [](IpcServer::ClientId) {});
    }

    REQUIRE(received_lines.size() == 1);
    CHECK(received_lines[0] == R"({"type":"start_recording"})");

    server.stop();
}

TEST_CASE("multiple simultaneously connected clients are each delivered their own lines") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    TestClient client_a(path);
    TestClient client_b(path);
    client_a.send_line("from a");
    client_b.send_line("from b");

    std::vector<std::pair<IpcServer::ClientId, std::string>> received;
    for (int i = 0; i < 20 && received.size() < 2; ++i) {
        server.poll_once(
            50,
            [&](IpcServer::ClientId id, const std::string& line) {
                received.emplace_back(id, line);
            },
            [](IpcServer::ClientId) {});
    }

    REQUIRE(received.size() == 2);
    CHECK(received[0].first != received[1].first);

    server.stop();
}

TEST_CASE("disconnecting a client triggers on_disconnect") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    TestClient client(path);
    client.send_line("hello");

    std::optional<IpcServer::ClientId> connected_id;
    for (int i = 0; i < 20 && !connected_id.has_value(); ++i) {
        server.poll_once(
            50, [&](IpcServer::ClientId id, const std::string&) { connected_id = id; },
            [](IpcServer::ClientId) {});
    }
    REQUIRE(connected_id.has_value());

    client.close_connection();

    std::optional<IpcServer::ClientId> disconnected_id;
    for (int i = 0; i < 20 && !disconnected_id.has_value(); ++i) {
        server.poll_once(
            50, [](IpcServer::ClientId, const std::string&) {},
            [&](IpcServer::ClientId id) { disconnected_id = id; });
    }

    REQUIRE(disconnected_id.has_value());
    CHECK(disconnected_id.value() == connected_id.value());

    server.stop();
}

TEST_CASE("send_line() delivers data back to the client") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    TestClient client(path);
    client.send_line("ping");

    std::optional<IpcServer::ClientId> connected_id;
    for (int i = 0; i < 20 && !connected_id.has_value(); ++i) {
        server.poll_once(
            50, [&](IpcServer::ClientId id, const std::string&) { connected_id = id; },
            [](IpcServer::ClientId) {});
    }
    REQUIRE(connected_id.has_value());

    server.send_line(connected_id.value(), R"({"type":"pong"})");

    const std::string reply = client.read_line();
    CHECK(reply == R"({"type":"pong"})");

    server.stop();
}

TEST_CASE("send_line() to a disconnected client is a silent no-op") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    server.send_line(999999, "nobody is listening");

    server.stop();
}

TEST_CASE("an unterminated line beyond the size cap disconnects the client") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    TestClient client(path);

    // No trailing newline, ever: keeps growing the server's per-client
    // buffer past the 1 MiB cap instead of ever completing a line. Sent
    // in small chunks interleaved with poll_once() so the server drains
    // its socket as it goes; a single multi-megabyte send() would block
    // the (blocking) test client waiting for buffer space the server
    // never frees, since it isn't polling yet at that point.
    const std::string chunk(4096, 'x');
    std::optional<IpcServer::ClientId> disconnected_id;
    for (int i = 0; i < 300 && !disconnected_id.has_value(); ++i) {
        client.send_raw(chunk);
        server.poll_once(
            10, [](IpcServer::ClientId, const std::string&) {},
            [&](IpcServer::ClientId id) { disconnected_id = id; });
    }

    REQUIRE(disconnected_id.has_value());

    server.stop();
}

TEST_CASE("send_line() disconnects a client that isn't reading fast enough") {
    const auto path = unique_test_socket_path();
    std::filesystem::remove(path);
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());

    TestClient client(path);
    client.send_line("hello");

    std::optional<IpcServer::ClientId> connected_id;
    for (int i = 0; i < 20 && !connected_id.has_value(); ++i) {
        server.poll_once(
            50, [&](IpcServer::ClientId id, const std::string&) { connected_id = id; },
            [](IpcServer::ClientId) {});
    }
    REQUIRE(connected_id.has_value());

    // Flood the client without it ever reading, until the kernel's send
    // buffer for this connection fills and a write() returns EAGAIN.
    const std::string chunk(8192, 'x');
    for (int i = 0; i < 2000; ++i) {
        server.send_line(connected_id.value(), chunk);
    }

    std::optional<IpcServer::ClientId> disconnected_id;
    for (int i = 0; i < 20 && !disconnected_id.has_value(); ++i) {
        server.poll_once(
            20, [](IpcServer::ClientId, const std::string&) {},
            [&](IpcServer::ClientId id) { disconnected_id = id; });
    }

    REQUIRE(disconnected_id.has_value());
    CHECK(disconnected_id.value() == connected_id.value());

    server.stop();
}
