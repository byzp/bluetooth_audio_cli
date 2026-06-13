#include "audio_service.h"

#include <iostream>
#include <algorithm>

using namespace winrt::Windows::Media::Audio;

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

AudioService::AudioService() = default;

AudioService::~AudioService() {
    CloseAllConnections();
}

// ---------------------------------------------------------------------------
// Thread-safe queries
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

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

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

void AudioService::UnsubscribeAndRemove(const std::string& deviceId) {
    // Caller must hold m_mutex.
    auto it = m_connections.find(deviceId);
    if (it == m_connections.end()) return;

    auto& conn = it->second;
    if (conn.connection && conn.state_token) {
        try {
            conn.connection.StateChanged(conn.state_token);
        } catch (...) {}
    }
    conn.connection = nullptr;
    conn.state_token = {};
    conn.state = State::Disconnected;
}

void AudioService::OnConnectionStateChanged(
    const std::string& deviceId,
    const AudioPlaybackConnection& sender,
    const winrt::Windows::Foundation::IInspectable& /*args*/) {

    auto connState = sender.State();
    bool shouldNotify = false;
    State newState = State::Disconnected;
    std::string detail;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(deviceId);
        if (it == m_connections.end()) return;

        auto& conn = it->second;

        if (connState == AudioPlaybackConnectionState::Opened) {
            if (!conn.final_notified && conn.state != State::Streaming) {
                conn.state = State::Streaming;
                conn.final_notified = true;
                newState = State::Streaming;
                shouldNotify = true;
            }
        } else {
            // Closed or other -> connection lost
            if (conn.state == State::Connected ||
                conn.state == State::Streaming ||
                conn.state == State::Connecting) {
                conn.state = State::Disconnected;
                conn.last_error = "Connection closed by remote device.";
                newState = State::Disconnected;
                detail = conn.last_error;
                shouldNotify = true;
            }
        }
    }

    if (shouldNotify) {
        InvokeCallback(deviceId, newState, detail);
    }
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool AudioService::OpenConnection(const std::string& deviceId, const std::string& deviceName) {
    // Check if already connected
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(deviceId);
        if (it != m_connections.end() && it->second.state != State::Disconnected) {
            return false; // Already connected
        }
    }

    // Mark as connecting
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& conn = m_connections[deviceId];
        conn.device_id   = deviceId;
        conn.device_name = deviceName;
        conn.state       = State::Connecting;
        conn.last_error.clear();
    }
    InvokeCallback(deviceId, State::Connecting, "");

    winrt::hstring hDeviceId = winrt::to_hstring(deviceId);

    for (int attempt = 1; attempt <= MaxRetries; ++attempt) {
        try {
            auto connection = AudioPlaybackConnection::TryCreateFromId(hDeviceId);

            if (!connection) {
                if (attempt == MaxRetries) {
                    std::string errMsg = "Could not create audio connection. "
                                         "Device may not support A2DP sink mode.";
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        auto it = m_connections.find(deviceId);
                        if (it != m_connections.end()) {
                            it->second.state = State::Error;
                            it->second.last_error = errMsg;
                        }
                    }
                    InvokeCallback(deviceId, State::Error, errMsg);
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        UnsubscribeAndRemove(deviceId);
                        m_connections.erase(deviceId);
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(RetryDelayMs));
                }
                continue;
            }

            // Store connection reference
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_connections.find(deviceId);
                if (it == m_connections.end()) {
                    auto& conn = m_connections[deviceId];
                    conn.device_id   = deviceId;
                    conn.device_name = deviceName;
                    conn.connection  = connection;
                } else {
                    it->second.connection = connection;
                }
            }

            // Subscribe to state changes (capture deviceId by value)
            std::string capturedId = deviceId;
            winrt::event_token token;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_connections.find(deviceId);
                if (it == m_connections.end()) return false;
                token = connection.StateChanged(
                    [this, capturedId](auto&& sender, auto&& args) {
                        OnConnectionStateChanged(capturedId, sender, args);
                    });
                it->second.state_token = token;
            }

            // Start and open
            connection.StartAsync().get();
            auto result = connection.OpenAsync().get();

            if (result.Status() != AudioPlaybackConnectionOpenResultStatus::Success) {
                auto extendedHr = result.ExtendedError();
                std::string extMsg;
                if (extendedHr) {
                    extMsg = winrt::to_string(winrt::hresult_error(extendedHr).message());
                } else {
                    extMsg = "No extended error";
                }

                if (attempt == MaxRetries) {
                    std::string errMsg = "Failed to open audio connection. Status: " +
                        std::to_string(static_cast<int>(result.Status())) + " (" + extMsg + ")";
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        auto it = m_connections.find(deviceId);
                        if (it != m_connections.end()) {
                            it->second.state = State::Error;
                            it->second.last_error = errMsg;
                        }
                    }
                    InvokeCallback(deviceId, State::Error, errMsg);
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        UnsubscribeAndRemove(deviceId);
                        m_connections.erase(deviceId);
                    }
                    return false;
                }

                // Retry
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto it = m_connections.find(deviceId);
                    if (it != m_connections.end()) {
                        if (it->second.connection && it->second.state_token) {
                            try { it->second.connection.StateChanged(it->second.state_token); } catch (...) {}
                            it->second.state_token = {};
                        }
                        it->second.connection = nullptr;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(RetryDelayMs));
                continue;
            }

            // Wait for Opened state
            bool isStreaming = false;
            for (int i = 0; i < (StateTimeoutMs / 50); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                try {
                    if (connection && connection.State() == AudioPlaybackConnectionState::Opened) {
                        isStreaming = true;
                        break;
                    }
                } catch (...) {
                    break;
                }
            }

            State finalState = isStreaming ? State::Streaming : State::Connected;
            bool shouldNotify = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_connections.find(deviceId);
                if (it != m_connections.end()) {
                    // Only notify if the StateChanged event hasn't already fired
                    // (avoids duplicate "[+] streaming" messages)
                    if (it->second.state == State::Connecting && !it->second.final_notified) {
                        it->second.state = finalState;
                        it->second.last_error.clear();
                        it->second.final_notified = true;
                        shouldNotify = true;
                    }
                }
            }
            if (shouldNotify) {
                InvokeCallback(deviceId, finalState, "");
            }
            return true;

        } catch (const winrt::hresult_error& ex) {
            if (attempt == MaxRetries) {
                std::string errMsg = "Error opening connection: " + winrt::to_string(ex.message());
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto it = m_connections.find(deviceId);
                    if (it != m_connections.end()) {
                        it->second.state = State::Error;
                        it->second.last_error = errMsg;
                    }
                }
                InvokeCallback(deviceId, State::Error, errMsg);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    UnsubscribeAndRemove(deviceId);
                    m_connections.erase(deviceId);
                }
                return false;
            }
        } catch (const std::exception& ex) {
            if (attempt == MaxRetries) {
                std::string errMsg = std::string("Error: ") + ex.what();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto it = m_connections.find(deviceId);
                    if (it != m_connections.end()) {
                        it->second.state = State::Error;
                        it->second.last_error = errMsg;
                    }
                }
                InvokeCallback(deviceId, State::Error, errMsg);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    UnsubscribeAndRemove(deviceId);
                    m_connections.erase(deviceId);
                }
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(RetryDelayMs));
    }

    std::string errMsg = "Could not establish connection after " + std::to_string(MaxRetries) + " attempts.";
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(deviceId);
        if (it != m_connections.end()) {
            it->second.state = State::Error;
            it->second.last_error = errMsg;
        }
    }
    InvokeCallback(deviceId, State::Error, errMsg);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        UnsubscribeAndRemove(deviceId);
        m_connections.erase(deviceId);
    }
    return false;
}

void AudioService::CloseConnection(const std::string& deviceId) {
    bool wasConnected = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(deviceId);
        if (it != m_connections.end() && it->second.state != State::Disconnected) {
            wasConnected = true;
            UnsubscribeAndRemove(deviceId);
        }
        m_connections.erase(deviceId);
    }
    if (wasConnected) {
        InvokeCallback(deviceId, State::Disconnected, "");
    }
}

void AudioService::CloseAllConnections() {
    std::vector<std::string> toRemove;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, conn] : m_connections) {
            UnsubscribeAndRemove(id);
            toRemove.push_back(id);
        }
        for (const auto& id : toRemove) {
            m_connections.erase(id);
        }
    }
    for (const auto& id : toRemove) {
        InvokeCallback(id, State::Disconnected, "");
    }
}
