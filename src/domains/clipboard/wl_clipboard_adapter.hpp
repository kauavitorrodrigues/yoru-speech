#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace yoru::clipboard {

// An error copying text to the clipboard, with a message suitable for
// logging. Never thrown: reported through copy()'s return value.
struct ClipboardError {
    std::string message;
};

// Copies text to the system clipboard by invoking wl-copy as a
// subprocess, feeding it the text on stdin. This is the only place in
// the project that knows wl-copy exists: the rest of the system depends
// only on copy()'s Result, so a different mechanism (a future X11 or
// portal-based adapter) could replace it without touching callers.
//
// wl-copy forks into the background to keep serving paste requests (a
// property of Wayland's clipboard model, not something this adapter
// manages), so the process this class waits on exits quickly regardless
// of how long the clipboard selection stays valid.
//
// copy() blocks the calling thread for up to `timeout`, at which point
// it kills the subprocess and reports an error. This is an accepted
// tradeoff for a rare, one-shot side effect on a local, trusted binary:
// making it non-blocking would add real complexity for a failure mode
// (a hung wl-copy) that isn't expected to occur in practice.
class WlClipboardAdapter {
public:
    // `command` and `timeout` are overridable for tests; production
    // code should use the defaults. 500ms comfortably covers a healthy
    // wl-copy invocation (typically low single-digit milliseconds) while
    // bounding how long a hung one can stall this single-threaded
    // service's entire IPC responsiveness, since copy() runs
    // synchronously inside the same call chain that answers every
    // client.
    explicit WlClipboardAdapter(std::string command = "wl-copy",
                                std::chrono::milliseconds timeout = std::chrono::milliseconds{500});

    // Runs `command` with `text` on its stdin. Returns an error if the
    // command can't be found or started, exits with a non-zero status,
    // or doesn't finish within `timeout` (in which case it is killed).
    std::optional<ClipboardError> copy(const std::string& text) const;

private:
    std::string command_;
    std::chrono::milliseconds timeout_;
};

} // namespace yoru::clipboard
