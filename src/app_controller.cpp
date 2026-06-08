#include "app_controller.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>

AppController::AppController()
    : m_settings(AppSettings::Load()) {
    m_bluetooth = std::make_unique<BluetoothService>();
    m_audio     = std::make_unique<AudioService>();
    m_volume    = std::make_unique<VolumeService>();

    // Apply saved volume
    m_volume->SetVolume(m_settings.volume);

    // Wire callbacks
    m_audio->SetOnStateChanged([this](const std::string& deviceId,
                                      AudioService::State state,
                                      const std::string& detail) {
        OnAudioStateChanged(deviceId, state, detail);
    });

    // Start device discovery
    m_bluetooth->StartWatching();
    m_bluetooth->WaitForEnumeration(3000);

    // Auto-connect if configured
    if (m_settings.auto_connect && !m_settings.last_device_id.empty()) {
        std::cout << "Auto-connecting to " << m_settings.last_device_name << "...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto devices = m_bluetooth->GetDevices();
        auto it = std::find_if(devices.begin(), devices.end(),
            [this](const BluetoothDeviceInfo& d) {
                return d.id == m_settings.last_device_id;
            });

        if (it != devices.end()) {
            bool ok = m_audio->OpenConnection(it->id, it->name);
            if (!ok) {
                std::cout << "Auto-connect failed: "
                          << m_audio->GetLastError(it->id) << "\n";
            }
        } else {
            std::cout << "Last device not found. Use 'list' to see available devices.\n";
        }
    }
}

AppController::~AppController() {
    Shutdown();
}

// ---------------------------------------------------------------------------
// CLI Commands
// ---------------------------------------------------------------------------

void AppController::ListDevices() {
    PrintDeviceList();
}

void AppController::ScanDevices() {
    std::cout << "Scanning for Bluetooth audio devices...\n";
    m_bluetooth->StopWatching();
    m_bluetooth->StartWatching();
    m_bluetooth->WaitForEnumeration(5000);

    auto devices = m_bluetooth->GetDevices();
    if (devices.empty()) {
        std::cout << "No Bluetooth audio devices found.\n";
        std::cout << "Make sure:\n";
        std::cout << "  - Bluetooth is enabled on this PC\n";
        std::cout << "  - Your audio source device is paired via Windows Bluetooth settings\n";
    } else {
        PrintDeviceList();
    }
}

void AppController::ConnectDevice(int index) {
    auto devices = m_bluetooth->GetDevices();

    if (index < 1 || static_cast<size_t>(index) > devices.size()) {
        std::cout << "Invalid device index. Use 'list' to see available devices.\n";
        return;
    }

    const auto& device = devices[static_cast<size_t>(index - 1)];

    // Check if already connected
    if (m_audio->IsConnected(device.id)) {
        std::cout << "Already connected to " << device.name << ".\n";
        return;
    }

    std::cout << "Connecting to " << device.name << "...\n";

    bool ok = m_audio->OpenConnection(device.id, device.name);

    if (ok) {
        std::cout << "Connected. Audio ready from " << device.name << ".\n";
        m_settings.last_device_id   = device.id;
        m_settings.last_device_name = device.name;
        m_settings.Save();
    } else {
        std::cout << "Connection failed: " << m_audio->GetLastError(device.id) << "\n";
    }
}

void AppController::DisconnectDevice(int index) {
    auto active = m_audio->GetActiveConnections();

    if (active.empty()) {
        std::cout << "No active connections.\n";
        return;
    }

    if (index < 1 || static_cast<size_t>(index) > active.size()) {
        // Disconnect all
        std::cout << "Disconnecting all " << active.size() << " connection(s)...\n";
        m_audio->CloseAllConnections();
        std::cout << "All connections closed.\n";
        return;
    }

    // Disconnect specific
    const auto& conn = active[static_cast<size_t>(index - 1)];
    std::cout << "Disconnecting " << conn.device_name << "...\n";
    m_audio->CloseConnection(conn.device_id);
    std::cout << "Disconnected.\n";
}

void AppController::ShowStatus() {
    auto active = m_audio->GetActiveConnections();
    int volume = m_volume->GetVolume();
    bool muted = m_volume->IsMuted();

    // Active connections
    if (active.empty()) {
        std::cout << "Active Connections : None\n";
    } else {
        std::cout << "Active Connections : " << active.size() << "\n";
        for (size_t i = 0; i < active.size(); ++i) {
            const auto& c = active[i];
            std::cout << "  [" << (i + 1) << "] "
                      << std::left << std::setw(25) << c.device_name
                      << "  " << AudioService::StateToString(c.state) << "\n";
            if (!c.last_error.empty()) {
                std::cout << "      Error: " << c.last_error << "\n";
            }
        }
    }

    std::cout << "System Volume      : " << volume << "%\n";
    std::cout << "Muted              : " << (muted ? "Yes" : "No") << "\n";
    std::cout << "Auto-Connect       : " << (m_settings.auto_connect ? "On" : "Off") << "\n";

    // Also show available (paired but not connected) device count
    auto devices = m_bluetooth->GetDevices();
    std::cout << "Paired Devices     : " << devices.size() << "\n";
}

void AppController::SetVolume(int vol) {
    m_volume->SetVolume(vol);
    m_settings.volume = m_volume->GetVolume();
    m_settings.Save();
    std::cout << "Volume set to " << m_settings.volume << "%.\n";
}

void AppController::ToggleMute() {
    bool currently = m_volume->IsMuted();
    m_volume->SetMute(!currently);
    std::cout << (m_volume->IsMuted() ? "Muted." : "Unmuted.") << "\n";
}

void AppController::SetAutoConnect(bool enabled) {
    m_settings.auto_connect = enabled;
    m_settings.Save();
    std::cout << "Auto-connect " << (enabled ? "enabled" : "disabled") << ".\n";
    if (enabled && !m_settings.last_device_id.empty()) {
        std::cout << "Will auto-connect to: " << m_settings.last_device_name << "\n";
    }
}

void AppController::ShowHelp() const {
    std::cout << "\n";
    std::cout << "Available commands:\n";
    std::cout << "  list | ls              Show paired Bluetooth audio devices\n";
    std::cout << "  scan | refresh         Rescan for devices\n";
    std::cout << "  connect <n> | conn <n> Connect to device by index (supports multiple)\n";
    std::cout << "  disconnect | disc      Disconnect all active connections\n";
    std::cout << "  disconnect <n> | disc <n>  Disconnect specific active connection\n";
    std::cout << "  status | stat           Show all connections, volume, and device info\n";
    std::cout << "  volume <0-100> | vol   Set system master volume\n";
    std::cout << "  mute                    Toggle mute on/off\n";
    std::cout << "  autoconnect on|off      Enable/disable auto-connect on start\n";
    std::cout << "  help | ?                Show this help\n";
    std::cout << "  quit | exit | q         Exit application\n";
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------

void AppController::PrintDeviceList() const {
    auto devices = m_bluetooth->GetDevices();

    if (devices.empty()) {
        std::cout << "No devices found. Use 'scan' to refresh.\n";
        return;
    }

    std::cout << "\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];

        // Determine status considering multi-connection
        std::string status = "Paired";
        if (m_audio->IsConnected(d.id)) {
            // Check exact state from active connections
            auto active = m_audio->GetActiveConnections();
            auto it = std::find_if(active.begin(), active.end(),
                [&](const AudioService::ConnectionStatus& c) {
                    return c.device_id == d.id;
                });
            if (it != active.end()) {
                status = AudioService::StateToString(it->state);
            } else {
                status = "Connected";
            }
        } else if (d.is_connected) {
            status = "Connected";
        }

        std::cout << "[" << std::setw(2) << (i + 1) << "] "
                  << std::left << std::setw(30) << d.name
                  << "  " << status << "\n";
    }
    std::cout << "\n";
}

void AppController::SaveDevice(const std::string& deviceId) {
    auto devices = m_bluetooth->GetDevices();
    auto it = std::find_if(devices.begin(), devices.end(),
        [&](const BluetoothDeviceInfo& d) { return d.id == deviceId; });
    if (it != devices.end()) {
        m_settings.last_device_id   = it->id;
        m_settings.last_device_name = it->name;
        m_settings.Save();
    }
}

void AppController::OnAudioStateChanged(const std::string& deviceId,
                                         AudioService::State state,
                                         const std::string& detail) {
    // Find device name
    std::string deviceName = deviceId;
    {
        auto devices = m_bluetooth->GetDevices();
        auto it = std::find_if(devices.begin(), devices.end(),
            [&](const BluetoothDeviceInfo& d) { return d.id == deviceId; });
        if (it != devices.end()) {
            deviceName = it->name;
        }
    }

    switch (state) {
        case AudioService::State::Connecting:
            // Already printed by ConnectDevice()
            break;

        case AudioService::State::Connected:
            std::cout << "\n[+] " << deviceName << " connected.\n> " << std::flush;
            SaveDevice(deviceId);
            break;

        case AudioService::State::Streaming:
            std::cout << "\n[+] " << deviceName << " streaming.\n> " << std::flush;
            SaveDevice(deviceId);
            break;

        case AudioService::State::Disconnected:
            std::cout << "\n[-] " << deviceName;
            if (!detail.empty()) std::cout << " - " << detail;
            std::cout << "\n> " << std::flush;
            break;

        case AudioService::State::Error:
            std::cerr << "\n[!] " << deviceName << " error: " << detail << "\n> " << std::flush;
            break;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool AppController::IsRunning() const {
    return m_running.load();
}

void AppController::Shutdown() {
    if (!m_running.exchange(false)) {
        return;
    }

    m_audio->CloseAllConnections();
    m_bluetooth->StopWatching();
    m_settings.volume = m_volume->GetVolume();
    m_settings.Save();
}
