#include "core/logger.hpp"

// Service entry point.
//
// At this early stage the executable only initializes logging, signals
// startup, and exits. The daemon lifecycle, argument parsing, and event loop
// are introduced in later roadmap phases.
int main() {
    const yoru::core::Logger logger("main");
    logger.info("Yoru Speech started");
    return 0;
}
