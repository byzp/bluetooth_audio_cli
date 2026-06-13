#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>

#ifdef _WIN32
#include <thread>
#include <chrono>
#include <winrt/Windows.Media.Audio.h>
#include <winrt/Windows.Foundation.h>
#else
#include <thread>
#include <atomic>
#include <systemd/sd-bus.h>
#endif

/// Manages multiple simultaneous Bluetooth A2DP audio playback connections.
///
/// Windows: Windows.Media.Audio.AudioPlaybackConnection (the OS audio sink).
/// Linux:   BlueZ org.bluez.Device1.Connect/Disconnect over D-Bus; the audio
///          stream itself is carried by the system audio server (PipeWire /
///          PulseAudio). Connection liveness is tracked via PropertiesChanged.
class AudioService {
public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Streaming,
        Error
    };

    /// Snapshot of one active connection for status display.
    struct ConnectionStatus {
        std::string device_id;
        std::string device_name;
        State       state = State::Disconnected;
        std::string last_error;
    };

    /// Callback signature: deviceId, new state, detail message.
    using StateCallback = std::function<void(
        const std::string& deviceId, State state, const std::string& detail)>;

    AudioService();
    ~AudioService();

    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;

    /// Open an audio playback connection to the specified device.
    /// Existing connections are preserved (multiple concurrent connections).
    /// Returns true on success, false if already connected or on failure.
    bool OpenConnection(const std::string& deviceId, const std::string& deviceName);

    /// Close the connection for a specific device.
    void CloseConnection(const std::string& deviceId);

    /// Close all active connections.
    void CloseAllConnections();

    /// Snapshot of all active (non-disconnected) connections.
    std::vector<ConnectionStatus> GetActiveConnections() const;

    /// Total active connection count.
    int GetConnectionCount() const;

    /// Check if a specific device is currently connected / connecting / streaming.
    bool IsConnected(const std::string& deviceId) const;

    /// Last error for a specific device.
    std::string GetLastError(const std::string& deviceId) const;

    /// Register a callback for per-device state changes.
    void SetOnStateChanged(StateCallback cb);

    static const char* StateToString(State s);

private:
    struct ActiveConnection {
#ifdef _WIN32
        winrt::Windows::Media::Audio::AudioPlaybackConnection connection{ nullptr };
        winrt::event_token state_token;
#endif
        State       state           = State::Disconnected;
        bool        final_notified  = false;  // prevents duplicate "[+] streaming"
        std::string device_id;
        std::string device_name;
        std::string last_error;
    };

    std::map<std::string, ActiveConnection> m_connections;
    StateCallback           m_on_state_changed;
    mutable std::mutex      m_mutex;

    /// Safely invoke the user callback (copies under lock, invokes outside).
    void InvokeCallback(const std::string& deviceId, State state, const std::string& detail);

#ifdef _WIN32
    static constexpr int MaxRetries    = 3;
    static constexpr int RetryDelayMs  = 500;
    static constexpr int StateTimeoutMs = 1500;

    /// Unsubscribe events and remove entry from the map.
    void UnsubscribeAndRemove(const std::string& deviceId);

    /// WinRT StateChanged handler for a specific device.
    void OnConnectionStateChanged(
        const std::string& deviceId,
        const winrt::Windows::Media::Audio::AudioPlaybackConnection& sender,
        const winrt::Windows::Foundation::IInspectable& args);
#else
    // Two buses, each used by exactly one thread (sd-bus is not safe for
    // concurrent use): m_call_bus for synchronous Connect/Disconnect on the
    // CLI thread; m_event_bus for PropertiesChanged on the pump thread.
    sd_bus*           m_call_bus  = nullptr;
    sd_bus*           m_event_bus = nullptr;
    std::thread       m_pump_thread;
    std::atomic<bool> m_running{ false };

    void StartPump();
    void StopPump();
    void PumpLoop();
    void Disconnect(const std::string& deviceId);  // fire-and-forget Device1.Disconnect

    static int OnPropertiesChanged(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
#endif
};
