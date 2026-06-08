#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <thread>
#include <chrono>
#include <memory>

#include <winrt/Windows.Media.Audio.h>
#include <winrt/Windows.Foundation.h>

/// Manages multiple simultaneous Bluetooth A2DP audio playback connections
/// via Windows.Media.Audio.AudioPlaybackConnection.
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
        winrt::Windows::Media::Audio::AudioPlaybackConnection connection{ nullptr };
        winrt::event_token state_token;
        State       state           = State::Disconnected;
        bool        final_notified  = false;  // prevents duplicate "[+] streaming"
        std::string device_id;
        std::string device_name;
        std::string last_error;
    };

    std::map<std::string, ActiveConnection> m_connections;
    StateCallback           m_on_state_changed;
    mutable std::mutex      m_mutex;

    static constexpr int MaxRetries    = 3;
    static constexpr int RetryDelayMs  = 500;
    static constexpr int StateTimeoutMs = 1500;

    /// Unsubscribe events and remove entry from the map.
    void UnsubscribeAndRemove(const std::string& deviceId);

    /// Safely invoke the user callback (copies under lock, invokes outside).
    void InvokeCallback(const std::string& deviceId, State state, const std::string& detail);

    /// WinRT StateChanged handler for a specific device.
    void OnConnectionStateChanged(
        const std::string& deviceId,
        const winrt::Windows::Media::Audio::AudioPlaybackConnection& sender,
        const winrt::Windows::Foundation::IInspectable& args);
};
