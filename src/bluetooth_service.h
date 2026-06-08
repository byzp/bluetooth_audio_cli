#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Audio.h>
#include <winrt/Windows.Foundation.Collections.h>

/// Describes a paired Bluetooth audio device discovered via WinRT DeviceWatcher.
struct BluetoothDeviceInfo {
    std::string id;
    std::string name;
    bool        is_connected = false;
};

/// Event-driven Bluetooth audio device discovery service.
/// Uses Windows.Devices.Enumeration.DeviceWatcher with the
/// AudioPlaybackConnection device selector.
class BluetoothService {
public:
    using DeviceListCallback = std::function<void()>;

    BluetoothService();
    ~BluetoothService();

    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    /// Start watching for Bluetooth audio devices.
    void StartWatching();

    /// Stop the device watcher and release resources.
    void StopWatching();

    /// Thread-safe snapshot of currently known devices.
    std::vector<BluetoothDeviceInfo> GetDevices() const;

    /// Register a callback invoked whenever the device list changes.
    void SetOnDeviceListChanged(DeviceListCallback cb);

    /// Block until initial enumeration completes or timeout (ms) expires.
    void WaitForEnumeration(int timeoutMs = 3000);

private:
    winrt::Windows::Devices::Enumeration::DeviceWatcher m_watcher{ nullptr };

    std::vector<BluetoothDeviceInfo> m_devices;
    mutable std::mutex               m_mutex;
    std::condition_variable          m_enum_cv;
    bool                             m_enumeration_complete = false;

    DeviceListCallback m_on_changed;

    winrt::event_token m_added_token;
    winrt::event_token m_updated_token;
    winrt::event_token m_removed_token;
    winrt::event_token m_enum_completed_token;

    void UnsubscribeEvents();
};
