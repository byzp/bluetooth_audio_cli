#pragma once

#include <memory>
#include <string>
#include <atomic>

#include "bluetooth_service.h"
#include "audio_service.h"
#include "settings.h"

/// Top-level application controller.
/// Orchestrates BluetoothService, AudioService, and AppSettings.
/// Supports multiple simultaneous Bluetooth audio connections.
/// Provides a synchronous CLI command interface.
class AppController {
public:
    AppController();
    ~AppController();

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    // -- CLI Commands --

    /// Display the list of paired Bluetooth audio devices.
    void ListDevices();

    /// Rescan for devices (stop + restart the device watcher).
    void ScanDevices();

    /// Connect to a device by its 1-based index in the device list.
    /// Existing connections are preserved.
    void ConnectDevice(int index);

    /// Disconnect all active connections, or a specific one by index.
    /// index=0 disconnects all; index>=1 disconnects the nth active connection.
    void DisconnectDevice(int index);

    /// Show status of all active connections.
    void ShowStatus();

    /// Toggle auto-connect on/off.
    void SetAutoConnect(bool enabled);

    /// Display available commands.
    void ShowHelp() const;

    /// Whether the controller is in a running state.
    bool IsRunning() const;

    /// Graceful shutdown: disconnect all, save settings, stop watcher.
    void Shutdown();

private:
    std::unique_ptr<BluetoothService> m_bluetooth;
    std::unique_ptr<AudioService>     m_audio;
    AppSettings                       m_settings;
    std::atomic<bool>                 m_running{ true };

    // Internal helpers
    void PrintDeviceList() const;
    void SaveDevice(const std::string& deviceId);

    // Event handlers
    void OnAudioStateChanged(const std::string& deviceId,
                             AudioService::State state, const std::string& detail);
};
