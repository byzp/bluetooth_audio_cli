#include "audio_service.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// A2DP connection management via BlueZ (org.bluez.Device1).
//
// Unlike Windows (where AudioPlaybackConnection *is* the audio sink), on Linux
// the audio stream is owned by the system audio server (PipeWire / PulseAudio).
// This service merely drives the BlueZ device connection and tracks liveness:
//   OpenConnection  -> Device1.Connect   (synchronous, on m_call_bus)
//   CloseConnection -> Device1.Disconnect
//   liveness        -> PropertiesChanged "Connected" (on m_event_bus / pump)
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kBluez       = "org.bluez";
constexpr const char* kDeviceIface = "org.bluez.Device1";
constexpr const char* kProperties  = "org.freedesktop.DBus.Properties";

// Parse a PropertiesChanged body (sa{sv}as) far enough to extract "Connected".
void ReadConnectedChange(sd_bus_message* m, bool& haveConnected, bool& connected) {
    const char* iface = nullptr;
    if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &iface) < 0) return;
    if (!iface || strcmp(iface, kDeviceIface) != 0) return;

    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") <= 0) return;
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

}  // namespace

const char* AudioService::StateToString(State s) {
    switch (s) {
        case State::Disconnected: return "Disconnected";
        case State::Connecting:   return "Connecting";
        case State::Connected:    return "Connected";
        case State::Streaming:    return "Streaming";
        case State::Error:        return "Error";
        default:                  return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AudioService::AudioService() {
    if (sd_bus_open_system(&m_call_bus) < 0) {
        m_call_bus = nullptr;
    }
    StartPump();
}

AudioService::~AudioService() {
    CloseAllConnections();
    StopPump();
    if (m_call_bus) {
        sd_bus_flush_close_unref(m_call_bus);
        m_call_bus = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Thread-safe queries (identical to the Windows backend)
// ---------------------------------------------------------------------------

std::vector<AudioService::ConnectionStatus> AudioService::GetActiveConnections() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ConnectionStatus> result;
    result.reserve(m_connections.size());
    for (const auto& [id, conn] : m_connections) {
        if (conn.state != State::Disconnected) {
            result.push_back({ conn.device_id, conn.device_name, conn.state, conn.last_error });
        }
    }
    return result;
}

int AudioService::GetConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = 0;
    for (const auto& [id, conn] : m_connections) {
        if (conn.state != State::Disconnected) ++count;
    }
    return count;
}

bool AudioService::IsConnected(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connections.find(deviceId);
    return it != m_connections.end() && it->second.state != State::Disconnected;
}

std::string AudioService::GetLastError(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connections.find(deviceId);
    return (it != m_connections.end()) ? it->second.last_error : std::string{};
}

void AudioService::SetOnStateChanged(StateCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_state_changed = std::move(cb);
}

void AudioService::InvokeCallback(const std::string& deviceId, State state, const std::string& detail) {
    StateCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_on_state_changed;
    }
    if (cb) {
        cb(deviceId, state, detail);
    }
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool AudioService::OpenConnection(const std::string& deviceId, const std::string& deviceName) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(deviceId);
        if (it != m_connections.end() && it->second.state != State::Disconnected) {
            return false;  // Already connected
        }
        auto& conn = m_connections[deviceId];
        conn.device_id      = deviceId;
        conn.device_name    = deviceName;
        conn.state          = State::Connecting;
        conn.final_notified = false;
        conn.last_error.clear();
    }
    InvokeCallback(deviceId, State::Connecting, "");

    auto fail = [&](const std::string& msg) -> bool {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_connections.find(deviceId);
            if (it != m_connections.end()) {
                it->second.state      = State::Error;
                it->second.last_error = msg;
            }
        }
        InvokeCallback(deviceId, State::Error, msg);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connections.erase(deviceId);
        }
        return false;
    };

    if (!m_call_bus) {
        return fail("D-Bus system bus unavailable. Is bluetoothd running?");
    }

    // The device id is the BlueZ object path. Connect() blocks until all
    // auto-connect profiles (including A2DP) are connected, or it errors.
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_call_method(m_call_bus, kBluez, deviceId.c_str(),
                               kDeviceIface, "Connect", &err, &reply, "");

    bool ok = (r >= 0);
    std::string errMsg;
    if (!ok) {
        if (err.message)   errMsg = err.message;
        else if (err.name) errMsg = err.name;
        else               errMsg = "Connect failed.";
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);

    if (!ok) {
        return fail(errMsg);
    }

    // Connected. Mark streaming unless the PropertiesChanged handler beat us.
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(deviceId);
        if (it != m_connections.end() &&
            it->second.state == State::Connecting && !it->second.final_notified) {
            it->second.state          = State::Streaming;
            it->second.final_notified = true;
            it->second.last_error.clear();
            notify = true;
        }
    }
    if (notify) {
        InvokeCallback(deviceId, State::Streaming, "");
    }
    return true;
}

void AudioService::Disconnect(const std::string& deviceId) {
    if (!m_call_bus) return;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    sd_bus_call_method(m_call_bus, kBluez, deviceId.c_str(),
                       kDeviceIface, "Disconnect", &err, &reply, "");
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
}

void AudioService::CloseConnection(const std::string& deviceId) {
    bool wasConnected = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(deviceId);
        if (it != m_connections.end() && it->second.state != State::Disconnected) {
            wasConnected = true;
        }
        m_connections.erase(deviceId);  // erase first so the signal handler ignores it
    }
    Disconnect(deviceId);
    if (wasConnected) {
        InvokeCallback(deviceId, State::Disconnected, "");
    }
}

void AudioService::CloseAllConnections() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, conn] : m_connections) {
            ids.push_back(id);
        }
        m_connections.clear();
    }
    for (const auto& id : ids) {
        Disconnect(id);
        InvokeCallback(id, State::Disconnected, "");
    }
}

// ---------------------------------------------------------------------------
// Event pump (m_event_bus): watch Connected to detect remote disconnects
// ---------------------------------------------------------------------------

void AudioService::StartPump() {
    if (sd_bus_open_system(&m_event_bus) < 0 || !m_event_bus) {
        m_event_bus = nullptr;
        return;
    }
    sd_bus_match_signal(m_event_bus, nullptr, kBluez, nullptr,
                        kProperties, "PropertiesChanged",
                        &AudioService::OnPropertiesChanged, this);
    m_running = true;
    m_pump_thread = std::thread([this] { PumpLoop(); });
}

void AudioService::StopPump() {
    m_running = false;
    if (m_pump_thread.joinable()) {
        m_pump_thread.join();
    }
    if (m_event_bus) {
        sd_bus_flush_close_unref(m_event_bus);
        m_event_bus = nullptr;
    }
}

void AudioService::PumpLoop() {
    while (m_running.load()) {
        int r = sd_bus_process(m_event_bus, nullptr);
        if (r < 0) break;
        if (r > 0) continue;
        sd_bus_wait(m_event_bus, 200000ULL);  // microseconds
    }
}

int AudioService::OnPropertiesChanged(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<AudioService*>(userdata);

    const char* path = sd_bus_message_get_path(m);
    if (!path) return 0;

    bool haveConnected = false, connected = false;
    ReadConnectedChange(m, haveConnected, connected);
    if (!haveConnected) return 0;

    std::string id = path;
    State newState = State::Disconnected;
    std::string detail;
    bool notify = false;

    {
        std::lock_guard<std::mutex> lock(self->m_mutex);
        auto it = self->m_connections.find(id);
        if (it == self->m_connections.end()) {
            return 0;  // Not a device we manage.
        }
        if (!connected) {
            if (it->second.state != State::Disconnected) {
                it->second.last_error = "Connection closed by remote device.";
                detail   = it->second.last_error;
                newState = State::Disconnected;
                notify   = true;
            }
            self->m_connections.erase(it);
        } else if (it->second.state == State::Connecting && !it->second.final_notified) {
            it->second.state          = State::Streaming;
            it->second.final_notified = true;
            newState                  = State::Streaming;
            notify                    = true;
        }
    }

    if (notify) {
        self->InvokeCallback(id, newState, detail);
    }
    return 0;
}
