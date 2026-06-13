#include "platform.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>

#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#include <winrt/Windows.Foundation.h>

namespace {

// Signal flag for Ctrl+C / close handling.
std::atomic<bool> g_signaled{false};

// Saved timer period so Shutdown() can restore it.
UINT g_timerPeriod = 0;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT ||
        ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        g_signaled = true;
        return TRUE; // Signal handled
    }
    return FALSE;
}

} // namespace

namespace platform {

void Init() {
    // Set console to UTF-8 for proper Unicode device name display.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Initialize Windows Runtime (required for AudioPlaybackConnection API).
    try {
        winrt::init_apartment();
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Windows Runtime: " << e.what() << "\n";
        std::cerr << "This application requires Windows 10 version 2004 (build 19041) or later.\n";
        std::exit(1);
    }

    // --- Audio performance optimizations ---
    // 1ms timer resolution — critical for glitch-free multi-device audio.
    // Default Windows timer is 15.6ms, causing audio buffer underruns.
    TIMECAPS tc{};
    if (timeGetDevCaps(&tc, sizeof(tc)) == TIMERR_NOERROR) {
        g_timerPeriod = tc.wPeriodMin;  // Typically 1ms
        timeBeginPeriod(g_timerPeriod);
    }
    // Boost process priority to reduce scheduling latency for audio threads.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
}

void Shutdown() {
    // Restore timer resolution and priority.
    if (g_timerPeriod > 0) {
        timeEndPeriod(g_timerPeriod);
        g_timerPeriod = 0;
    }
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
}

void InstallSignalHandler() {
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "Warning: Could not register Ctrl+C handler.\n";
    }
}

bool ShouldExit() {
    return g_signaled.load();
}

} // namespace platform
