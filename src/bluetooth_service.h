#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>

#ifdef _WIN32
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Audio.h>
#include <winrt/Windows.Foundation.Collections.h>
#else
#include <thread>
#include <atomic>
#include <systemd/sd-bus.h>
#endif

/// Describes a paired Bluetooth audio device.
struct BluetoothDeviceInfo {
    std::string id;     // Windows: DeviceInformation.Id; Linux: BlueZ D-Bus object path
    std::string name;
    bool        is_connected = false;
};

/// Event-driven Bluetooth audio device discovery service.
///
/// Windows: Windows.Devices.Enumeration.DeviceWatcher with the
///          AudioPlaybackConnection device selector.
/// Linux:   BlueZ over D-Bus (org.bluez), enumerating org.bluez.Device1
///          objects and watching InterfacesAdded/Removed + PropertiesChanged.
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
    // --- Shared state ---
    std::vector<BluetoothDeviceInfo> m_devices;
    mutable std::mutex               m_mutex;
    std::condition_variable          m_enum_cv;
    bool                             m_enumeration_complete = false;
    DeviceListCallback               m_on_changed;

#ifdef _WIN32
    winrt::Windows::Devices::Enumeration::DeviceWatcher m_watcher{ nullptr };

    winrt::event_token m_added_token;
    winrt::event_token m_updated_token;
    winrt::event_token m_removed_token;
    winrt::event_token m_enum_completed_token;

    void UnsubscribeEvents();
#else
    // The bus is owned exclusively by the pump thread (sd-bus connections are
    // not safe for concurrent use from multiple threads). The CLI thread only
    // touches m_devices/m_enum_cv under m_mutex.
    sd_bus*           m_bus = nullptr;
    std::thread       m_pump_thread;
    std::atomic<bool> m_running{ false };

    void PumpLoop();          // runs on m_pump_thread: enumerate then process signals
    void EnumerateDevices();  // ObjectManager.GetManagedObjects -> m_devices

    // D-Bus signal handlers (userdata = this). Static so they match the
    // sd_bus_message_handler_t signature while retaining private access.
    static int OnInterfacesAdded(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int OnInterfacesRemoved(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int OnPropertiesChanged(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
#endif
};
