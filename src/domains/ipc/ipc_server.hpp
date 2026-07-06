#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace yoru::ipc {

// Resolves the default Unix Domain Socket path, honoring XDG_RUNTIME_DIR
// when set, falling back to /tmp/yoru-speech.sock otherwise.
std::filesystem::path default_socket_path();

// An error starting the server, with a message suitable for logging.
// Never thrown: reported through start()'s return value.
struct IpcError {
    std::string message;
};

// A Unix Domain Socket server delivering newline-delimited lines (JSON
// Lines, one message per line) from any number of simultaneously
// connected clients. Knows nothing about the message format itself, or
// which commands map to which messages: that belongs to the message
// codec and command dispatch layers introduced alongside the IPC
// commands. This is the transport only.
//
// Single-threaded: every method must be called from one controlling
// thread. Internally multiplexes the listening socket and every
// connected client's socket with poll(), so no client can block another
// (every socket, including accepted client sockets, is non-blocking).
//
// None of `on_line`/`on_disconnect` may call back into this same
// IpcServer (start()/stop()/poll_once()/send_line()): poll_once() is
// mid-iteration over its own bookkeeping when they run, and reentering it
// is not guarded against.
class IpcServer {
public:
    // Identifies a connected client for the lifetime of its connection.
    // This is the client's underlying file descriptor, which the kernel
    // is free to reuse for an unrelated later connection once this one
    // disconnects. Holding a ClientId past the poll_once() call that
    // handed it to you, across a point where that client could have
    // disconnected, risks send_line() silently reaching a different,
    // unrelated client that happened to be accepted with the same fd
    // number. Safe to use only within the same synchronous callback, or
    // immediately after accepting/receiving from that client.
    using ClientId = int;
    using LineCallback = std::function<void(ClientId, const std::string& line)>;
    using DisconnectCallback = std::function<void(ClientId)>;

    explicit IpcServer(std::filesystem::path socket_path = default_socket_path());
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

    // Removes a stale socket file left over from a previous run, then
    // creates, binds, and listens on the socket. Returns an error if the
    // path is too long for a Unix socket address, or the socket cannot
    // be created, bound, or listened on. Safe to call again after stop().
    std::optional<IpcError> start();

    // Closes every client connection, the listening socket, and removes
    // the socket file. Safe to call when not running (no-op). Also
    // called from the destructor, so a server destroyed while running
    // never leaks file descriptors or leaves the socket file behind.
    void stop();

    // Accepts every pending connection (the whole backlog, not just one)
    // and delivers every complete
    // newline-terminated line received since the last call via
    // `on_line` (the newline itself is not included). Calls
    // `on_disconnect` for clients that closed their connection or
    // errored. Waits for at most `timeout_ms` for activity if there is
    // none immediately (0 = return right away, negative = wait
    // indefinitely). Meant to be called repeatedly from the service's
    // own run loop. A no-op, without error, if the server isn't running.
    void poll_once(int timeout_ms, const LineCallback& on_line,
                   const DisconnectCallback& on_disconnect);

    // Writes `line` followed by a newline to the given client. A no-op,
    // without error, if that client is no longer connected: a client
    // racing a disconnect with an in-flight response is an expected
    // occurrence, not a failure. If the client isn't reading fast enough
    // to keep up, or the write fails outright, the client is scheduled
    // for disconnection: `on_disconnect` fires for it from the next
    // poll_once() call, never synchronously from here.
    void send_line(ClientId client_id, const std::string& line);

    bool is_running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace yoru::ipc
