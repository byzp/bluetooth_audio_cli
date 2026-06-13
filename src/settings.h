#pragma once

#include <string>
#include <filesystem>

/// Application settings persisted as JSON.
/// Windows: %APPDATA%/BluetoothAudioReceiver/settings.json
/// Linux:   $XDG_CONFIG_HOME (or ~/.config)/bluetooth_audio_receiver/settings.json
struct AppSettings {
    bool auto_connect = false;
    std::string last_device_id;
    std::string last_device_name;
    std::string language = "en";

    static AppSettings Load();
    void Save() const;

    AppSettings() = default;

private:
    static std::filesystem::path GetSettingsPath();
    static std::string EscapeJson(const std::string& s);
    static std::string GetJsonString(const std::string& json, const std::string& key);
    static bool GetJsonBool(const std::string& json, const std::string& key);
};
