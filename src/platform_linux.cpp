#include "platform.h"

#include <csignal>
#include <clocale>

namespace {

// Set from the signal handler; only async-signal-safe operations allowed there,
// so we use a volatile sig_atomic_t rather than std::atomic.
volatile sig_atomic_t g_signaled = 0;

void HandleSignal(int /*signum*/) {
    g_signaled = 1;
}

} // namespace

namespace platform {

void Init() {
    // Honor the user's locale so UTF-8 device names print correctly.
    // Linux terminals are UTF-8 by default; this is belt-and-suspenders.
    std::setlocale(LC_ALL, "");

    // No timer-resolution or process-priority tuning on Linux: the audio
    // stream is owned by the system audio server (PipeWire / PulseAudio),
    // which manages its own real-time scheduling. Our process is just a
    // control client and does not touch the audio path.
}

void Shutdown() {
    // Nothing to restore on Linux.
}

void InstallSignalHandler() {
    struct sigaction sa{};
    sa.sa_handler = HandleSignal;
    sigemptyset(&sa.sa_mask);
    // No SA_RESTART: a blocking read in the input loop should be interrupted
    // by Ctrl+C so the program can exit promptly.
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

bool ShouldExit() {
    return g_signaled != 0;
}

} // namespace platform
