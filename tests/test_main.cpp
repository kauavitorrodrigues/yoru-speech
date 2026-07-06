// Single translation unit that provides doctest's implementation and
// main(). All other test files only include <doctest/doctest.h>.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <csignal>

int main(int argc, char** argv) {
    // WlClipboardAdapter's tests exec real subprocesses (e.g. /usr/bin/true)
    // that can exit before this process finishes writing to their stdin
    // pipe; without ignoring SIGPIPE the way the daemon's own main() does,
    // that write can kill this entire test binary instead of just failing
    // an assertion.
    std::signal(SIGPIPE, SIG_IGN);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
