#include <iostream>
#include <string>
#include <sstream>
#include <atomic>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#include <winrt/Windows.Foundation.h>

#include "app_controller.h"

// Signal flag for Ctrl+C / close handling
static std::atomic<bool> g_signaled{false};

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT ||
        ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        g_signaled = true;
        return TRUE; // Signal handled
    }
    return FALSE;
}

static void PrintBanner() {
    std::cout << "\n";
    std::cout << "  Bluetooth Audio Receiver CLI\n";
    std::cout << "  ==============================\n";
    std::cout << "  Type 'help' for available commands.\n";
    std::cout << "  Press Ctrl+C to exit.\n\n";
}

int main(int argc, char* argv[]) {
    // Check for --help / --version
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "Bluetooth Audio Receiver CLI\n\n";
            std::cout << "Usage: bluetooth_audio_cli [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --help, -h       Show this help\n";
            std::cout << "  --version, -v    Show version\n\n";
            std::cout << "Requirements:\n";
            std::cout << "  Windows 10 version 2004+ or Windows 11\n";
            std::cout << "  Bluetooth adapter with A2DP support\n";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "Bluetooth Audio Receiver CLI v1.0.0\n";
            return 0;
        }
    }

    // Set console to UTF-8 for proper Unicode device name display
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Initialize Windows Runtime (required for AudioPlaybackConnection API)
    try {
        winrt::init_apartment();
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Windows Runtime: " << e.what() << "\n";
        std::cerr << "This application requires Windows 10 version 2004 (build 19041) or later.\n";
        return 1;
    }

    // Register Ctrl+C handler for graceful shutdown
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "Warning: Could not register Ctrl+C handler.\n";
    }

    // --- Audio performance optimizations ---
    // 1ms timer resolution — critical for glitch-free multi-device audio.
    // Default Windows timer is 15.6ms, causing audio buffer underruns.
    UINT timerPeriod = 0;
    TIMECAPS tc{};
    if (timeGetDevCaps(&tc, sizeof(tc)) == TIMERR_NOERROR) {
        timerPeriod = tc.wPeriodMin;  // Typically 1ms
        timeBeginPeriod(timerPeriod);
    }
    // Boost process priority to reduce scheduling latency for audio threads.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    PrintBanner();

    // Create the application controller (starts device discovery)
    AppController* controller = nullptr;
    try {
        controller = new AppController();
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize: " << e.what() << "\n";
        return 1;
    }

    // CLI input loop
    while (controller->IsRunning() && !g_signaled) {
        std::cout << "> " << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        // Convert to lowercase for case-insensitive matching
        for (auto& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (cmd == "list" || cmd == "ls") {
            controller->ListDevices();

        } else if (cmd == "scan" || cmd == "refresh") {
            controller->ScanDevices();

        } else if (cmd == "connect" || cmd == "conn") {
            int idx = -1;
            iss >> idx;
            if (idx > 0) {
                controller->ConnectDevice(idx);
            } else {
                std::cout << "Usage: connect <device-index>\n";
            }

        } else if (cmd == "disconnect" || cmd == "disc") {
            int idx = 0;  // 0 = all, >=1 = specific
            iss >> idx;
            controller->DisconnectDevice(idx);

        } else if (cmd == "status" || cmd == "stat") {
            controller->ShowStatus();

        } else if (cmd == "volume" || cmd == "vol") {
            int vol = -1;
            iss >> vol;
            if (vol >= 0 && vol <= 100) {
                controller->SetVolume(vol);
            } else {
                std::cout << "Usage: volume <0-100>\n";
            }

        } else if (cmd == "mute") {
            controller->ToggleMute();

        } else if (cmd == "autoconnect") {
            std::string arg;
            iss >> arg;
            for (auto& c : arg) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (arg == "on" || arg == "true" || arg == "1" || arg == "enable") {
                controller->SetAutoConnect(true);
            } else if (arg == "off" || arg == "false" || arg == "0" || arg == "disable") {
                controller->SetAutoConnect(false);
            } else {
                std::cout << "Usage: autoconnect on|off\n";
            }

        } else if (cmd == "help" || cmd == "?") {
            controller->ShowHelp();

        } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            break;

        } else {
            std::cout << "Unknown command: " << cmd << ". Type 'help' for available commands.\n";
        }
    }

    // Graceful shutdown
    controller->Shutdown();
    delete controller;

    // Restore timer resolution
    if (timerPeriod > 0) {
        timeEndPeriod(timerPeriod);
    }
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    std::cout << "\nGoodbye!\n";
    return 0;
}
