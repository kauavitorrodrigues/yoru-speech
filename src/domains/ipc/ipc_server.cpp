#include "domains/ipc/ipc_server.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace yoru::ipc {

namespace {

constexpr int kListenBacklog = 16;
constexpr std::size_t kReadChunkSize = 4096;
// A client that sends this many bytes without a newline is treated as
// misbehaving and disconnected, rather than letting its unterminated
// line grow the server's memory without bound.
constexpr std::size_t kMaxLineBytes = 1 << 20; // 1 MiB

std::string errno_message(const char* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

void set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return;
    }
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool interrupted() {
    return errno == EINTR;
}

} // namespace

std::filesystem::path default_socket_path() {
    if (const char* xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR")) {
        return std::filesystem::path(xdg_runtime_dir) / "yoru-speech.sock";
    }
    return "/tmp/yoru-speech.sock";
}

struct IpcServer::Impl {
    std::filesystem::path socket_path;
    int listen_fd = -1;
    // Also the registry of currently-connected client fds: a client is
    // "known" exactly while it has an entry here, from accept() to
    // disconnect.
    std::unordered_map<int, std::string> read_buffers;
    // Clients send_line() gave up on (a write that would block, meaning
    // the client isn't reading fast enough, or a hard write error).
    // Disconnected for real, with on_disconnect notified, at the start of
    // the next poll_once() call: on_disconnect always fires from there,
    // never synchronously from inside send_line().
    std::vector<int> pending_disconnects;
};

IpcServer::IpcServer(std::filesystem::path socket_path) : impl_(std::make_unique<Impl>()) {
    impl_->socket_path = std::move(socket_path);
}

IpcServer::~IpcServer() {
    stop();
}

std::optional<IpcError> IpcServer::start() {
    if (impl_->listen_fd != -1) {
        return IpcError{"server is already running"};
    }

    const std::string path_string = impl_->socket_path.string();
    sockaddr_un address{};
    if (path_string.size() >= sizeof(address.sun_path)) {
        return IpcError{"socket path is too long for a Unix domain socket: " + path_string};
    }

    // Remove a stale socket file left over from a previous run. A
    // missing file is not an error; any other failure means the
    // subsequent bind() will fail with a clearer, syscall-specific
    // message anyway, so it isn't checked here.
    ::unlink(path_string.c_str());

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        return IpcError{errno_message("failed to create socket")};
    }

    address.sun_family = AF_UNIX;
    // address is zero-initialized above, so sun_path is already
    // null-terminated even in the boundary case where strncpy's source
    // is exactly sizeof(sun_path) - 1 bytes and writes no terminator of
    // its own.
    std::strncpy(address.sun_path, path_string.c_str(), sizeof(address.sun_path) - 1);

    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        const auto error = errno_message("failed to bind socket");
        ::close(fd);
        return IpcError{error};
    }

    if (::listen(fd, kListenBacklog) == -1) {
        const auto error = errno_message("failed to listen on socket");
        ::close(fd);
        ::unlink(path_string.c_str());
        return IpcError{error};
    }

    // Non-blocking throughout: a client that stops reading must never be
    // able to block accept()/read()/write() and stall every other client
    // on this single-threaded server.
    set_nonblocking(fd);

    impl_->listen_fd = fd;
    return std::nullopt;
}

void IpcServer::stop() {
    if (impl_->listen_fd == -1) {
        return;
    }

    for (const auto& entry : impl_->read_buffers) {
        ::close(entry.first);
    }
    impl_->read_buffers.clear();
    impl_->pending_disconnects.clear();

    ::close(impl_->listen_fd);
    impl_->listen_fd = -1;
    ::unlink(impl_->socket_path.string().c_str());
}

void IpcServer::poll_once(int timeout_ms, const LineCallback& on_line,
                          const DisconnectCallback& on_disconnect) {
    if (impl_->listen_fd == -1) {
        return;
    }

    // Process write failures from previous send_line() calls before
    // anything else, so a doomed fd is never polled or read from again.
    for (const int fd : impl_->pending_disconnects) {
        if (impl_->read_buffers.erase(fd) > 0) {
            ::close(fd);
            on_disconnect(fd);
        }
    }
    impl_->pending_disconnects.clear();

    std::vector<pollfd> fds;
    fds.push_back(pollfd{impl_->listen_fd, POLLIN, 0});
    for (const auto& entry : impl_->read_buffers) {
        fds.push_back(pollfd{entry.first, POLLIN, 0});
    }

    if (::poll(fds.data(), fds.size(), timeout_ms) <= 0) {
        return;
    }

    if ((fds[0].revents & POLLIN) != 0) {
        // The listening socket is non-blocking, so this drains every
        // connection already queued in the backlog this round, rather
        // than accepting one and leaving the rest until the next call.
        for (;;) {
            const int client_fd = ::accept(impl_->listen_fd, nullptr, nullptr);
            if (client_fd == -1) {
                // EINTR (a signal arrived) and ECONNABORTED (the peer
                // reset the connection before we could accept it) are
                // both transient: retry rather than treating them as
                // "backlog drained." Any other error (including the
                // expected EAGAIN once the backlog truly is empty) stops
                // the drain for this poll_once() call.
                if (interrupted() || errno == ECONNABORTED) {
                    continue;
                }
                break;
            }
            set_nonblocking(client_fd);
            impl_->read_buffers[client_fd];
        }
    }

    std::vector<int> disconnected;
    std::array<char, kReadChunkSize> chunk{};
    for (std::size_t i = 1; i < fds.size(); ++i) {
        if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            continue;
        }

        const int fd = fds[i].fd;
        const ssize_t bytes_read = ::read(fd, chunk.data(), chunk.size());
        if (bytes_read == 0) {
            disconnected.push_back(fd);
            continue;
        }
        if (bytes_read < 0) {
            if (would_block() || interrupted()) {
                continue; // Nothing to read right now after all.
            }
            disconnected.push_back(fd);
            continue;
        }

        std::string& buffer = impl_->read_buffers[fd];
        buffer.append(chunk.data(), static_cast<std::size_t>(bytes_read));

        std::size_t newline_pos = 0;
        while ((newline_pos = buffer.find('\n')) != std::string::npos) {
            const std::string line = buffer.substr(0, newline_pos);
            buffer.erase(0, newline_pos + 1);
            on_line(fd, line);
        }

        if (buffer.size() > kMaxLineBytes) {
            disconnected.push_back(fd);
        }
    }

    for (const int fd : disconnected) {
        if (impl_->read_buffers.erase(fd) > 0) {
            ::close(fd);
            on_disconnect(fd);
        }
    }
}

void IpcServer::send_line(ClientId client_id, const std::string& line) {
    if (!impl_->read_buffers.contains(client_id)) {
        return;
    }

    const std::string data = line + "\n";
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t sent = ::write(client_id, data.data() + written, data.size() - written);
        if (sent < 0 && interrupted()) {
            continue; // A signal arrived before any bytes were sent; retry.
        }
        if (sent <= 0) {
            // Either a hard write error, or (would_block()) the client
            // isn't reading fast enough to keep up; either way, don't
            // block this single-threaded server waiting for it. Marked
            // for a real disconnect, with on_disconnect notified, at the
            // start of the next poll_once() call. Only recorded once:
            // repeated failed sends to the same doomed client (e.g. a
            // slow reader hit by many queued messages before the next
            // poll_once() runs) would otherwise pile up duplicate
            // entries here, harmlessly but needlessly.
            auto& pending = impl_->pending_disconnects;
            if (std::find(pending.begin(), pending.end(), client_id) == pending.end()) {
                pending.push_back(client_id);
            }
            return;
        }
        written += static_cast<std::size_t>(sent);
    }
}

bool IpcServer::is_running() const {
    return impl_->listen_fd != -1;
}

} // namespace yoru::ipc
