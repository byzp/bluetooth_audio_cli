# 蓝牙音频接收器 CLI

**Windows 与 Linux** 命令行蓝牙 A2DP 音频接收器。可同时接收多个蓝牙设备的音频 —— 在 Windows 上通过 `AudioPlaybackConnection` API，在 Linux 上通过 BlueZ（D-Bus）+ PipeWire/PulseAudio。

## 功能

- 蓝牙 A2DP（高级音频分发协议）接收模式
- 支持同时连接多个设备
- 启动时自动连接上次使用的设备
- 低开销 -- 事件驱动的设备发现，无需轮询
- 纯命令行，无界面依赖
- 跨平台：统一的命令与配置模型，各系统使用原生后端

## 系统要求

**Windows**
- Windows 10（版本 2004 / 构建 19041）或 Windows 11
- 支持 A2DP 的蓝牙适配器
- 已配对的音频源设备（手机、平板等）

**Linux**
- BlueZ（`bluetoothd`）以及已配对的音频源设备
- 已启用 **A2DP sink（接收端）** 的音频服务器：PipeWire（含 `libspa-0.2-bluetooth`）或加载了蓝牙模块的 PulseAudio
- 运行时需要 `libsystemd`（sd-bus）；基于 systemd 的发行版均自带

## 从源码构建

### Windows

```powershell
# 前提：安装 Visual Studio 2019 或 2022，包含 C++/WinRT 支持
# Windows SDK 10.0.19041.0 或更高版本

cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

或直接运行 `build.bat`。输出文件：`build\Release\bluetooth_audio_cli.exe`

### Linux

```bash
# 前提（Debian/Ubuntu）：
sudo apt install build-essential cmake pkg-config libsystemd-dev
# Fedora： sudo dnf install gcc-c++ cmake pkgconf-pkg-config systemd-devel
# Arch：   sudo pacman -S base-devel cmake systemd

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

或直接运行 `./build.sh`。输出文件：`build/bluetooth_audio_cli`

## 使用方法

运行可执行文件（Windows 为 `bluetooth_audio_cli.exe`，Linux 为 `./build/bluetooth_audio_cli`）：

### 命令列表

| 命令 | 说明 |
|---|---|
| `list`, `ls` | 显示已配对的蓝牙音频设备 |
| `scan`, `refresh` | 重新扫描设备 |
| `connect <n>`, `conn <n>` | 按序号连接设备（支持多个同时连接） |
| `disconnect`, `disc` | 断开所有活动连接 |
| `disconnect <n>`, `disc <n>` | 断开指定活动连接 |
| `status`, `stat` | 显示全部连接状态与设备信息 |
| `autoconnect on\|off` | 启用/禁用启动时自动连接 |
| `help`, `?` | 显示帮助 |
| `quit`, `exit`, `q` | 退出程序 |

### 示例

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

## 配置文件

设置以 JSON 格式保存：

- Windows：`%APPDATA%\BluetoothAudioReceiver\settings.json`
- Linux：`$XDG_CONFIG_HOME/bluetooth_audio_receiver/settings.json`（或 `~/.config/bluetooth_audio_receiver/settings.json`）

```json
{
    "auto_connect": true,
    "last_device_id": "...",
    "last_device_name": "Pixel 7",
    "language": "en"
}
```

- `auto_connect` -- 是否在启动时自动连接上次设备
- `last_device_id` / `last_device_name` -- 连接成功后自动保存
- `language` -- 界面语言（目前仅英语，保留字段）

## 项目架构

CLI 主循环、命令调度与配置均与平台无关。每个平台相关服务由「共享头文件 + 各系统实现」组成，由 CMake 按平台选择编译。

```
src/
  main.cpp                       入口、CLI 循环（平台无关）
  platform.h                     平台门面：初始化、关闭、信号处理
  platform_win.cpp               Windows：WinRT 初始化、UTF-8 控制台、定时器/优先级、Ctrl+C
  platform_linux.cpp             Linux：locale + SIGINT/SIGTERM
  app_controller.h/cpp           命令调度与状态管理（平台无关）
  settings.h/cpp                 JSON 配置持久化（平台无关；配置路径分平台）
  bluetooth_service.h            设备发现接口（共享）
  bluetooth_service_win.cpp        Windows：WinRT DeviceWatcher
  bluetooth_service_linux.cpp      Linux：BlueZ ObjectManager + 信号（sd-bus）
  audio_service.h                A2DP 连接接口（共享）
  audio_service_win.cpp            Windows：AudioPlaybackConnection（多连接）
  audio_service_linux.cpp          Linux：BlueZ Device1.Connect/Disconnect（sd-bus）
```

## 技术说明

- **多连接（Windows）**：使用多个 `AudioPlaybackConnection` 实例，每个连接具有独立的 `StateChanged` 事件处理器。回调模式避免死锁（加锁、复制回调、解锁、调用）。
- **音频优化（Windows）**：调用 `timeBeginPeriod(1)` 将系统定时器精度从默认 15.6ms 提升至 1ms，并设置 `HIGH_PRIORITY_CLASS` 进程优先级，减少音频缓冲区欠载。Linux 上音频通路由 PipeWire/PulseAudio 负责，其自行管理实时调度。
- **Linux 后端**：通过 sd-bus 与 BlueZ 通信。设备发现使用 `ObjectManager.GetManagedObjects`，并在独立的事件线程上监听 `InterfacesAdded`/`InterfacesRemoved`/`PropertiesChanged` 信号；连接由 `org.bluez.Device1.Connect`/`Disconnect` 驱动。实际音频流经由系统音频服务器传输。
- **依赖**：无第三方库。Windows 仅使用 Windows SDK（C++/WinRT）；Linux 链接 `libsystemd`（sd-bus）以访问 D-Bus。

## 多设备卡顿说明（Windows）

同时连接两个蓝牙设备播放音频时出现卡顿，主要原因：

1. **蓝牙带宽**：单个蓝牙适配器在 2.4GHz 频段上的可用带宽有限。两个 A2DP 流共享同一无线电资源，容易相互干扰。
2. **Windows 蓝牙协议栈**：系统蓝牙驱动和协议栈在处理多个 A2DP 连接时的调度效率有限。

本程序已通过以下方式尽力优化：
- 1ms 定时器精度（`timeBeginPeriod`），减少音频调度延迟
- 高进程优先级（`HIGH_PRIORITY_CLASS`），降低线程调度抖动

如果卡顿仍然明显，建议：
- 使用更高性能的蓝牙适配器（如 Intel AX210）
- 使用 USB 蓝牙适配器代替主板内置蓝牙，减少主板电路干扰
- 将蓝牙适配器远离 USB 3.0 设备（USB 3.0 会产生 2.4GHz 频段干扰）
