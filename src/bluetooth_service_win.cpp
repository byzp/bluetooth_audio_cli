#include "bluetooth_service.h"

#include <algorithm>
#include <iostream>

using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Media::Audio;

BluetoothService::BluetoothService() = default;

BluetoothService::~BluetoothService() {
    StopWatching();
}

std::vector<BluetoothDeviceInfo> BluetoothService::GetDevices() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices;
}

void BluetoothService::SetOnDeviceListChanged(DeviceListCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_changed = std::move(cb);
}

void BluetoothService::WaitForEnumeration(int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_enum_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                       [this] { return m_enumeration_complete; });
}

void BluetoothService::StartWatching() {
    if (m_watcher) {
        return; // Already watching
    }

    try {
        // Selector for devices that support AudioPlaybackConnection (A2DP Sink)
        winrt::hstring selector = AudioPlaybackConnection::GetDeviceSelector();

        // Request the IsConnected property so we can show device state
        auto extraProperties = winrt::single_threaded_vector<winrt::hstring>();
        extraProperties.Append(L"System.Devices.Aep.IsConnected");

        m_watcher = DeviceInformation::CreateWatcher(
            selector,
            extraProperties,
            DeviceInformationKind::AssociationEndpoint);

        m_added_token = m_watcher.Added(
            [this](DeviceWatcher const& /*sender*/, DeviceInformation const& args) {
                BluetoothDeviceInfo info;
                info.id   = winrt::to_string(args.Id());
                info.name = winrt::to_string(args.Name());
                if (info.name.empty()) {
                    info.name = "Unknown Device";
                }

                // Read IsConnected property
                auto props = args.Properties();
                auto key   = winrt::hstring(L"System.Devices.Aep.IsConnected");
                if (props.HasKey(key)) {
                    try {
                        auto val = props.Lookup(key);
                        info.is_connected = winrt::unbox_value<bool>(val);
                    } catch (...) {
                        info.is_connected = false;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    // Replace if already present, otherwise add
                    auto it = std::find_if(m_devices.begin(), m_devices.end(),
                        [&](const BluetoothDeviceInfo& d) { return d.id == info.id; });
                    if (it != m_devices.end()) {
                        *it = std::move(info);
                    } else {
                        m_devices.push_back(std::move(info));
                    }
                }
                if (m_on_changed) m_on_changed();
            });

        m_updated_token = m_watcher.Updated(
            [this](DeviceWatcher const& /*sender*/, DeviceInformationUpdate const& args) {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::find_if(m_devices.begin(), m_devices.end(),
                    [&](const BluetoothDeviceInfo& d) {
                        return d.id == winrt::to_string(args.Id());
                    });
                if (it == m_devices.end()) return;

                auto props = args.Properties();
                auto key   = winrt::hstring(L"System.Devices.Aep.IsConnected");
                if (props.HasKey(key)) {
                    try {
                        auto val = props.Lookup(key);
                        it->is_connected = winrt::unbox_value<bool>(val);
                    } catch (...) {}
                }
                // Note: m_on_changed is NOT invoked here to avoid flooding
                // during initial enumeration.
            });

        m_removed_token = m_watcher.Removed(
            [this](DeviceWatcher const& /*sender*/, DeviceInformationUpdate const& args) {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto id = winrt::to_string(args.Id());
                m_devices.erase(
                    std::remove_if(m_devices.begin(), m_devices.end(),
                        [&](const BluetoothDeviceInfo& d) { return d.id == id; }),
                    m_devices.end());
                if (m_on_changed) m_on_changed();
            });

        m_enum_completed_token = m_watcher.EnumerationCompleted(
            [this](DeviceWatcher const& /*sender*/, winrt::Windows::Foundation::IInspectable const& /*args*/) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_enumeration_complete = true;
                }
                m_enum_cv.notify_all();
                if (m_on_changed) m_on_changed();
            });

        m_enumeration_complete = false;
        m_watcher.Start();
    } catch (...) {
        StopWatching();
    }
}

void BluetoothService::StopWatching() {
    UnsubscribeEvents();

    if (m_watcher) {
        try {
            auto status = m_watcher.Status();
            if (status == DeviceWatcherStatus::Started ||
                status == DeviceWatcherStatus::EnumerationCompleted ||
                status == DeviceWatcherStatus::Stopping) {
                m_watcher.Stop();
            }
        } catch (...) {}
        m_watcher = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_devices.clear();
        m_enumeration_complete = false;
    }
}

void BluetoothService::UnsubscribeEvents() {
    if (!m_watcher) return;

    try {
        m_watcher.Added(m_added_token);
    } catch (...) {}
    try {
        m_watcher.Updated(m_updated_token);
    } catch (...) {}
    try {
        m_watcher.Removed(m_removed_token);
    } catch (...) {}
    try {
        m_watcher.EnumerationCompleted(m_enum_completed_token);
    } catch (...) {}
}
