#pragma once

#include <string>
#include <filesystem>

/// Application settings persisted as JSON in %APPDATA%/BluetoothAudioReceiver/settings.json
struct AppSettings {
    int volume = 100;
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
    static int GetJsonInt(const std::string& json, const std::string& key);
    static bool GetJsonBool(const std::string& json, const std::string& key);
};
