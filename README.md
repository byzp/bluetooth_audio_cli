# Bluetooth Audio Receiver CLI

[中文介绍](https://github.com/byzp/bluetooth_audio_cli/blob/main/README_zh.md)

Command-line Bluetooth A2DP audio sink for **Windows and Linux**. Receive audio from multiple Bluetooth devices simultaneously — via the Windows `AudioPlaybackConnection` API on Windows, or via BlueZ (D-Bus) + PipeWire/PulseAudio on Linux.

## Features

- Bluetooth A2DP (Advanced Audio Distribution Profile) sink mode
- Multiple simultaneous device connections
- Auto-connect to last used device
- Low overhead -- event-driven device discovery, no polling
- Pure CLI, no GUI dependencies
- Cross-platform: one CLI, one settings model, native backend per OS

## Requirements

**Windows**
- Windows 10 (version 2004 / build 19041) or Windows 11
- Bluetooth adapter with A2DP support
- Paired audio source device (phone, tablet, etc.)

**Linux**
- BlueZ (`bluetoothd`) and a paired audio source device
- An audio server configured for **A2DP sink**: PipeWire (with `libspa-0.2-bluetooth`) or PulseAudio with the Bluetooth modules
- `libsystemd` at runtime (sd-bus); present on systemd-based distributions

## Building from Source

### Windows

```powershell
# Prerequisites: Visual Studio 2019 or 2022 with C++/WinRT support
# Windows SDK 10.0.19041.0 or later

cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Alternatively, run `build.bat`. Output: `build\Release\bluetooth_audio_cli.exe`

### Linux

```bash
# Prerequisites (Debian/Ubuntu):
sudo apt install build-essential cmake pkg-config libsystemd-dev
# Fedora: sudo dnf install gcc-c++ cmake pkgconf-pkg-config systemd-devel
# Arch:   sudo pacman -S base-devel cmake systemd

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Alternatively, run `./build.sh`. Output: `build/bluetooth_audio_cli`

## Usage

Run the executable (`bluetooth_audio_cli.exe` on Windows, `./build/bluetooth_audio_cli` on Linux):

### Commands

| Command | Description |
|---|---|
| `list`, `ls` | Show paired Bluetooth audio devices |
| `scan`, `refresh` | Rescan for devices |
| `connect <n>`, `conn <n>` | Connect to device by index (supports multiple) |
| `disconnect`, `disc` | Disconnect all active connections |
| `disconnect <n>`, `disc <n>` | Disconnect specific active connection |
| `status`, `stat` | Show all connections and device info |
| `autoconnect on\|off` | Enable/disable auto-connect on startup |
| `help`, `?` | Show help |
| `quit`, `exit`, `q` | Exit application |

### Example

```
> list

[ 1] Pixel 7                         Paired
[ 2] Galaxy Tab S9                   Paired

> connect 1
Connecting to Pixel 7...
Connected. Audio ready from Pixel 7.

> connect 2
Connecting to Galaxy Tab S9...
Connected. Audio ready from Galaxy Tab S9.

> status
Active Connections : 2
  [1] Pixel 7                     Streaming
  [2] Galaxy Tab S9               Streaming
Auto-Connect       : Off
Paired Devices     : 3
```

## Settings

Settings are stored as JSON:

- Windows: `%APPDATA%\BluetoothAudioReceiver\settings.json`
- Linux: `$XDG_CONFIG_HOME/bluetooth_audio_receiver/settings.json` (or `~/.config/bluetooth_audio_receiver/settings.json`)

```json
{
    "auto_connect": true,
    "last_device_id": "...",
    "last_device_name": "Pixel 7",
    "language": "en"
}
```

- `auto_connect` -- Whether to auto-connect to `last_device_id` on startup
- `last_device_id` / `last_device_name` -- Saved on successful connection
- `language` -- UI language (currently English only; reserved for future use)

## Architecture

The CLI loop, command orchestration, and settings are platform-agnostic. Each
platform-specific service has a shared header and a per-OS implementation,
selected by CMake.

```
src/
  main.cpp                       Entry point, CLI loop (platform-agnostic)
  platform.h                     Façade: init, shutdown, signal handling
  platform_win.cpp               Windows: WinRT init, UTF-8 console, timer/priority, Ctrl+C
  platform_linux.cpp             Linux: locale + SIGINT/SIGTERM
  app_controller.h/cpp           Command orchestration and state (platform-agnostic)
  settings.h/cpp                 JSON persistence (platform-agnostic; per-OS config path)
  bluetooth_service.h            Device discovery interface (shared)
  bluetooth_service_win.cpp        Windows: WinRT DeviceWatcher
  bluetooth_service_linux.cpp      Linux: BlueZ ObjectManager + signals (sd-bus)
  audio_service.h                A2DP connection interface (shared)
  audio_service_win.cpp            Windows: AudioPlaybackConnection (multi-connection)
  audio_service_linux.cpp          Linux: BlueZ Device1.Connect/Disconnect (sd-bus)
```

## Technical Notes

- **Multi-connection (Windows)**: Uses multiple `AudioPlaybackConnection` instances, each with its own `StateChanged` event handler. Deadlock-free callback pattern (lock, copy callback, unlock, invoke).
- **Audio performance (Windows)**: Calls `timeBeginPeriod(1)` for 1ms system timer resolution (default 15.6ms) and sets `HIGH_PRIORITY_CLASS` to minimize audio buffer underruns. On Linux the audio path is owned by PipeWire/PulseAudio, which manages its own real-time scheduling.
- **Linux backend**: BlueZ over D-Bus via sd-bus. Device discovery uses `ObjectManager.GetManagedObjects` plus `InterfacesAdded`/`InterfacesRemoved`/`PropertiesChanged` signals on a dedicated pump thread; connections are driven by `org.bluez.Device1.Connect`/`Disconnect`. The actual audio stream flows through the system audio server.
- **Dependencies**: No third-party libraries. Windows uses only the Windows SDK (C++/WinRT). Linux links `libsystemd` (sd-bus) for D-Bus.
