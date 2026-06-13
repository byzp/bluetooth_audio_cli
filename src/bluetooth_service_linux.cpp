#include "bluetooth_service.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <strings.h>  // strncasecmp

// ---------------------------------------------------------------------------
// BlueZ over D-Bus (org.bluez)
//
// Discovery mirrors the Windows DeviceWatcher: an initial snapshot via
// ObjectManager.GetManagedObjects, then live updates from InterfacesAdded /
// InterfacesRemoved and per-device PropertiesChanged signals. All bus I/O runs
// on a single pump thread; the CLI thread only reads m_devices under m_mutex.
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kBluez       = "org.bluez";
constexpr const char* kDeviceIface = "org.bluez.Device1";
constexpr const char* kObjManager  = "org.freedesktop.DBus.ObjectManager";
constexpr const char* kProperties  = "org.freedesktop.DBus.Properties";

// A2DP-related service UUIDs (short 16-bit form embedded in the BT base UUID):
// 0x110A AudioSource, 0x110B AudioSink, 0x110D AdvancedAudioDistribution.
bool IsAudioUuid(const char* uuid) {
    if (!uuid) return false;
    return strncasecmp(uuid, "0000110a-", 9) == 0 ||
           strncasecmp(uuid, "0000110b-", 9) == 0 ||
           strncasecmp(uuid, "0000110d-", 9) == 0;
}

// Decide whether a device belongs in the list. We want paired audio devices,
// mirroring the AudioPlaybackConnection selector on Windows. We stay lenient:
// if a device's services aren't resolved yet (no UUIDs), include it.
bool ShouldInclude(bool paired, bool hasUuids, bool isAudio, const std::string& icon) {
    if (!paired) return false;
    if (isAudio) return true;
    if (!hasUuids) return true;  // services not yet resolved — don't hide it
    return icon == "audio-card" || icon == "audio-headset" ||
           icon == "audio-headphones" || icon == "phone" ||
           icon == "multimedia-player";
}

// Read an a{sv} property dictionary for org.bluez.Device1, at the current
// message position. Fully consumes the array on success.
void ReadDevice1Props(sd_bus_message* m,
                      std::string& name, std::string& alias, std::string& address,
                      std::string& icon, bool& paired, bool& connected,
                      bool& hasUuids, bool& isAudio) {
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") < 0) return;

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key) < 0 || !key) {
            sd_bus_message_exit_container(m);
            continue;
        }

        if (strcmp(key, "Alias") == 0 || strcmp(key, "Name") == 0 ||
            strcmp(key, "Address") == 0 || strcmp(key, "Icon") == 0) {
            const char* val = nullptr;
            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s") > 0) {
                if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &val) >= 0 && val) {
                    if      (strcmp(key, "Alias") == 0)   alias   = val;
                    else if (strcmp(key, "Name") == 0)    name    = val;
                    else if (strcmp(key, "Address") == 0) address = val;
                    else                                  icon    = val;
                }
                sd_bus_message_exit_container(m);
            } else {
                sd_bus_message_skip(m, "v");
            }
        } else if (strcmp(key, "Paired") == 0 || strcmp(key, "Connected") == 0) {
            int val = 0;
            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b") > 0) {
                if (sd_bus_message_read_basic(m, SD_BUS_TYPE_BOOLEAN, &val) >= 0) {
                    if (strcmp(key, "Paired") == 0) paired    = (val != 0);
                    else                            connected = (val != 0);
                }
                sd_bus_message_exit_container(m);
            } else {
                sd_bus_message_skip(m, "v");
            }
        } else if (strcmp(key, "UUIDs") == 0) {
            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "as") > 0) {
                if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") > 0) {
                    hasUuids = true;
                    const char* uuid = nullptr;
                    while (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &uuid) > 0) {
                        if (IsAudioUuid(uuid)) isAudio = true;
                    }
                    sd_bus_message_exit_container(m);
                }
                sd_bus_message_exit_container(m);
            } else {
                sd_bus_message_skip(m, "v");
            }
        } else {
            sd_bus_message_skip(m, "v");
        }

        sd_bus_message_exit_container(m);  // dict entry
    }

    sd_bus_message_exit_container(m);  // array
}

std::string PickName(const std::string& alias, const std::string& name,
                     const std::string& address) {
    if (!alias.empty())   return alias;
    if (!name.empty())    return name;
    if (!address.empty()) return address;
    return "Unknown Device";
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

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
    if (m_bus) {
        return;  // Already watching
    }

    if (sd_bus_open_system(&m_bus) < 0 || !m_bus) {
        m_bus = nullptr;
        return;
    }

    // Floating matches (slot == nullptr): freed automatically when the bus is
    // unreffed in StopWatching().
    sd_bus_match_signal(m_bus, nullptr, kBluez, nullptr,
                        kObjManager, "InterfacesAdded",
                        &BluetoothService::OnInterfacesAdded, this);
    sd_bus_match_signal(m_bus, nullptr, kBluez, nullptr,
                        kObjManager, "InterfacesRemoved",
                        &BluetoothService::OnInterfacesRemoved, this);
    sd_bus_match_signal(m_bus, nullptr, kBluez, nullptr,
                        kProperties, "PropertiesChanged",
                        &BluetoothService::OnPropertiesChanged, this);

    m_enumeration_complete = false;
    m_running = true;
    m_pump_thread = std::thread([this] { PumpLoop(); });
}

void BluetoothService::StopWatching() {
    m_running = false;
    if (m_pump_thread.joinable()) {
        m_pump_thread.join();
    }
    if (m_bus) {
        sd_bus_flush_close_unref(m_bus);  // also frees the floating match slots
        m_bus = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_devices.clear();
        m_enumeration_complete = false;
    }
}

// ---------------------------------------------------------------------------
// Pump thread
// ---------------------------------------------------------------------------

void BluetoothService::PumpLoop() {
    // Initial snapshot.
    EnumerateDevices();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enumeration_complete = true;
    }
    m_enum_cv.notify_all();

    DeviceListCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_on_changed;
    }
    if (cb) cb();

    // Process signals until stopped. Poll with a 200ms timeout so we can
    // observe m_running and shut down promptly.
    while (m_running.load()) {
        int r = sd_bus_process(m_bus, nullptr);
        if (r < 0) break;
        if (r > 0) continue;            // handled a message; check for more
        sd_bus_wait(m_bus, 200000ULL);  // microseconds
    }
}

void BluetoothService::EnumerateDevices() {
    sd_bus_message* reply = nullptr;
    sd_bus_error err = SD_BUS_ERROR_NULL;

    int r = sd_bus_call_method(m_bus, kBluez, "/", kObjManager,
                               "GetManagedObjects", &err, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&err);
        return;
    }

    std::vector<BluetoothDeviceInfo> found;

    // Signature: a{oa{sa{sv}}}
    if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{oa{sa{sv}}}") >= 0) {
        while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "oa{sa{sv}}") > 0) {
            const char* path = nullptr;
            sd_bus_message_read_basic(reply, SD_BUS_TYPE_OBJECT_PATH, &path);

            sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
            while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") > 0) {
                const char* iface = nullptr;
                sd_bus_message_read_basic(reply, SD_BUS_TYPE_STRING, &iface);

                if (iface && strcmp(iface, kDeviceIface) == 0) {
                    std::string name, alias, address, icon;
                    bool paired = false, connected = false, hasUuids = false, isAudio = false;
                    ReadDevice1Props(reply, name, alias, address, icon,
                                     paired, connected, hasUuids, isAudio);
                    if (ShouldInclude(paired, hasUuids, isAudio, icon)) {
                        BluetoothDeviceInfo info;
                        info.id           = path ? path : "";
                        info.name         = PickName(alias, name, address);
                        info.is_connected = connected;
                        found.push_back(std::move(info));
                    }
                } else {
                    sd_bus_message_skip(reply, "a{sv}");
                }

                sd_bus_message_exit_container(reply);  // dict entry sa{sv}
            }
            sd_bus_message_exit_container(reply);  // array a{sa{sv}}
            sd_bus_message_exit_container(reply);  // dict entry oa{sa{sv}}
        }
        sd_bus_message_exit_container(reply);  // outer array
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_devices = std::move(found);
    }
}

// ---------------------------------------------------------------------------
// Signal handlers (run on the pump thread)
// ---------------------------------------------------------------------------

int BluetoothService::OnInterfacesAdded(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BluetoothService*>(userdata);

    const char* path = nullptr;
    if (sd_bus_message_read_basic(m, SD_BUS_TYPE_OBJECT_PATH, &path) < 0) return 0;
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sa{sv}}") < 0) return 0;

    bool changed = false;
    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") > 0) {
        const char* iface = nullptr;
        sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &iface);

        if (iface && strcmp(iface, kDeviceIface) == 0) {
            std::string name, alias, address, icon;
            bool paired = false, connected = false, hasUuids = false, isAudio = false;
            ReadDevice1Props(m, name, alias, address, icon,
                             paired, connected, hasUuids, isAudio);
            if (ShouldInclude(paired, hasUuids, isAudio, icon)) {
                BluetoothDeviceInfo info;
                info.id           = path ? path : "";
                info.name         = PickName(alias, name, address);
                info.is_connected = connected;

                std::lock_guard<std::mutex> lock(self->m_mutex);
                auto it = std::find_if(self->m_devices.begin(), self->m_devices.end(),
                    [&](const BluetoothDeviceInfo& d) { return d.id == info.id; });
                if (it != self->m_devices.end()) {
                    *it = std::move(info);
                } else {
                    self->m_devices.push_back(std::move(info));
                }
                changed = true;
            }
        } else {
            sd_bus_message_skip(m, "a{sv}");
        }

        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    if (changed) {
        DeviceListCallback cb;
        {
            std::lock_guard<std::mutex> lock(self->m_mutex);
            cb = self->m_on_changed;
        }
        if (cb) cb();
    }
    return 0;
}

int BluetoothService::OnInterfacesRemoved(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BluetoothService*>(userdata);

    const char* path = nullptr;
    if (sd_bus_message_read_basic(m, SD_BUS_TYPE_OBJECT_PATH, &path) < 0) return 0;

    bool isDevice = false;
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") > 0) {
        const char* iface = nullptr;
        while (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &iface) > 0) {
            if (iface && strcmp(iface, kDeviceIface) == 0) isDevice = true;
        }
        sd_bus_message_exit_container(m);
    }

    if (isDevice && path) {
        DeviceListCallback cb;
        {
            std::lock_guard<std::mutex> lock(self->m_mutex);
            std::string id = path;
            self->m_devices.erase(
                std::remove_if(self->m_devices.begin(), self->m_devices.end(),
                    [&](const BluetoothDeviceInfo& d) { return d.id == id; }),
                self->m_devices.end());
            cb = self->m_on_changed;
        }
        if (cb) cb();
    }
    return 0;
}

int BluetoothService::OnPropertiesChanged(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BluetoothService*>(userdata);

    const char* iface = nullptr;
    if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &iface) < 0) return 0;
    if (!iface || strcmp(iface, kDeviceIface) != 0) return 0;

    const char* path = sd_bus_message_get_path(m);

    bool haveConnected = false, connected = false;
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") > 0) {
        while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key);
            if (key && strcmp(key, "Connected") == 0) {
                int val = 0;
                if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b") > 0) {
                    if (sd_bus_message_read_basic(m, SD_BUS_TYPE_BOOLEAN, &val) >= 0) {
                        haveConnected = true;
                        connected = (val != 0);
                    }
                    sd_bus_message_exit_container(m);
                } else {
                    sd_bus_message_skip(m, "v");
                }
            } else {
                sd_bus_message_skip(m, "v");
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }

    if (haveConnected && path) {
        // Mirror the Windows Updated handler: update state silently, no notify.
        std::lock_guard<std::mutex> lock(self->m_mutex);
        std::string id = path;
        auto it = std::find_if(self->m_devices.begin(), self->m_devices.end(),
            [&](const BluetoothDeviceInfo& d) { return d.id == id; });
        if (it != self->m_devices.end()) {
            it->is_connected = connected;
        }
    }
    return 0;
}
