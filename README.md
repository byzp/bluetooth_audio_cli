# Bluetooth Audio Receiver CLI

[中文介绍](https://github.com/byzp/bluetooth_audio_cli/blob/master/README_zh.md)

Command-line Bluetooth A2DP audio sink for Windows. Receive audio from multiple Bluetooth devices simultaneously via the Windows AudioPlaybackConnection API.

## Features

- Bluetooth A2DP (Advanced Audio Distribution Profile) sink mode
- Multiple simultaneous device connections
- System volume and mute control
- Auto-connect to last used device
- Low overhead -- event-driven device discovery, no polling
- Pure CLI, no GUI dependencies
- Audio-optimized: 1ms timer resolution, high process priority

## Requirements

- Windows 10 (version 2004 / build 19041) or Windows 11
- Bluetooth adapter with A2DP support
- Paired audio source device (phone, tablet, etc.)

## Building from Source

```powershell
# Prerequisites: Visual Studio 2019 or 2022 with C++/WinRT support
# Windows SDK 10.0.19041.0 or later

cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Alternatively, run `build.bat`.

Output: `build\Release\bluetooth_audio_cli.exe`

## Usage

Run the executable:

```powershell
.\bluetooth_audio_cli.exe
```

### Commands

| Command | Description |
|---|---|
| `list`, `ls` | Show paired Bluetooth audio devices |
| `scan`, `refresh` | Rescan for devices |
| `connect <n>`, `conn <n>` | Connect to device by index (supports multiple) |
| `disconnect`, `disc` | Disconnect all active connections |
| `disconnect <n>`, `disc <n>` | Disconnect specific active connection |
| `status`, `stat` | Show all connections, volume, mute, device info |
| `volume <0-100>`, `vol` | Set system master volume |
| `mute` | Toggle mute on/off |
| `autoconnect on\|off` | Enable/disable auto-connect on startup |
| `help`, `?` | Show help |
| `quit`, `exit`, `q` | Exit application |

### Example

```
> list

[ 1] Pixel 7                         Paired
[2 ] Galaxy Tab S9                   Paired

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
System Volume      : 85%
Muted              : No
Auto-Connect       : Off
Paired Devices     : 3
```

## Settings

Settings are stored as JSON at `%APPDATA%\BluetoothAudioReceiver\settings.json`:

```json
{
    "volume": 85,
    "auto_connect": true,
    "last_device_id": "...",
    "last_device_name": "Pixel 7",
    "language": "en"
}
```

- `volume` -- Last system volume level (0-100)
- `auto_connect` -- Whether to auto-connect to `last_device_id` on startup
- `last_device_id` / `last_device_name` -- Saved on successful connection
- `language` -- UI language (currently English only; reserved for future use)

## Architecture

```
src/
  main.cpp                 Entry point, CLI loop, audio optimizations
  app_controller.h/cpp     Command orchestration and state management
  bluetooth_service.h/cpp  WinRT DeviceWatcher for device discovery
  audio_service.h/cpp      WinRT AudioPlaybackConnection (multi-connection)
  volume_service.h/cpp     Core Audio COM for system volume/mute
  settings.h/cpp           JSON persistence to %APPDATA%
```

## Technical Notes

- **Multi-connection**: Uses multiple `AudioPlaybackConnection` instances, each with its own `StateChanged` event handler. Deadlock-free callback pattern (lock, copy callback, unlock, invoke).
- **Audio performance**: Calls `timeBeginPeriod(1)` for 1ms system timer resolution (default 15.6ms) and sets `HIGH_PRIORITY_CLASS` to minimize audio buffer underruns.
- **Dependencies**: Zero external libraries. Uses only Windows SDK APIs (C++/WinRT, Core Audio COM).
