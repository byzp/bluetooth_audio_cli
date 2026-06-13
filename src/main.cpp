#include <iostream>
#include <string>
#include <sstream>
#include <cctype>

#include "app_controller.h"
#include "platform.h"

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
            std::cout << "  A Bluetooth adapter with A2DP support and a paired audio source device.\n";
            std::cout << "  Windows 10 version 2004+ / Windows 11, or\n";
            std::cout << "  Linux with BlueZ and PipeWire/PulseAudio configured for A2DP sink.\n";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "Bluetooth Audio Receiver CLI v1.0.0\n";
            return 0;
        }
    }

    // Platform setup: console encoding, runtime init, performance tuning.
    platform::Init();

    // Register interrupt handler for graceful shutdown (Ctrl+C / SIGINT).
    platform::InstallSignalHandler();

    PrintBanner();

    // Create the application controller (starts device discovery)
    AppController* controller = nullptr;
    try {
        controller = new AppController();
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize: " << e.what() << "\n";
        platform::Shutdown();
        return 1;
    }

    // CLI input loop
    while (controller->IsRunning() && !platform::ShouldExit()) {
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

    platform::Shutdown();

    std::cout << "\nGoodbye!\n";
    return 0;
}
