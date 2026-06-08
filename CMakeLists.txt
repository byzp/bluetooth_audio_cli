cmake_minimum_required(VERSION 3.20)
project(BluetoothAudioCli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows 10 version 2004 SDK (build 19041) required for AudioPlaybackConnection API.
# Visual Studio 2019+ with C++/WinRT support is required.
# In VS 2022, C++/WinRT is built-in.
# For VS 2019, install the "C++/WinRT" extension via the VS Installer.

add_executable(bluetooth_audio_cli
    src/main.cpp
    src/app_controller.cpp
    src/bluetooth_service.cpp
    src/audio_service.cpp
    src/volume_service.cpp
    src/settings.cpp
)

target_include_directories(bluetooth_audio_cli PRIVATE src)

# Windows system libraries
target_link_libraries(bluetooth_audio_cli PRIVATE
    ole32
    runtimeobject
)

# Target Windows 10 2004+ for AudioPlaybackConnection API
set_target_properties(bluetooth_audio_cli PROPERTIES
    VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0"
)
