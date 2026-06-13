#include "settings.h"

#include <fstream>
#include <cctype>
#include <cstdlib>
#include <iostream>

namespace fs = std::filesystem;

fs::path AppSettings::GetSettingsPath() {
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? fs::path(appdata) : fs::current_path();
    return base / "BluetoothAudioReceiver" / "settings.json";
}

std::string AppSettings::EscapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
        }
    }
    return result;
}

std::string AppSettings::GetJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return "";

    // Find opening quote
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";

    // Find closing quote (handle escaped quotes)
    std::string value;
    for (size_t i = pos + 1; i < json.length(); ++i) {
        if (json[i] == '\\' && i + 1 < json.length()) {
            ++i; // skip escape, take next char
            switch (json[i]) {
                case '"':  value += '"';  break;
                case '\\': value += '\\'; break;
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                default:   value += json[i]; break;
            }
        } else if (json[i] == '"') {
            break;
        } else {
            value += json[i];
        }
    }
    return value;
}

bool AppSettings::GetJsonBool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return false;

    // Skip colon and whitespace
    ++pos;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
    }

    if (pos + 4 <= json.length() && json.substr(pos, 4) == "true") return true;
    return false;
}

AppSettings AppSettings::Load() {
    auto path = GetSettingsPath();

    if (!fs::exists(path)) {
        return AppSettings{};
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return AppSettings{};
    }

    try {
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        AppSettings settings;
        settings.auto_connect = GetJsonBool(content, "auto_connect");
        settings.last_device_id = GetJsonString(content, "last_device_id");
        settings.last_device_name = GetJsonString(content, "last_device_name");
        settings.language = GetJsonString(content, "language");
        if (settings.language.empty()) settings.language = "en";

        return settings;
    } catch (...) {
        return AppSettings{};
    }
}

void AppSettings::Save() const {
    auto path = GetSettingsPath();
    auto dir = path.parent_path();

    try {
        if (!fs::exists(dir)) {
            fs::create_directories(dir);
        }

        std::string json;
        json += "{\n";
        json += "    \"auto_connect\": " + std::string(auto_connect ? "true" : "false") + ",\n";
        json += "    \"last_device_id\": \"" + EscapeJson(last_device_id) + "\",\n";
        json += "    \"last_device_name\": \"" + EscapeJson(last_device_name) + "\",\n";
        json += "    \"language\": \"" + EscapeJson(language) + "\"\n";
        json += "}\n";

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (file.is_open()) {
            file.write(json.data(), static_cast<std::streamsize>(json.size()));
        }
    } catch (...) {
        // Silently ignore save errors
    }
}
