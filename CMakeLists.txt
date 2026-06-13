cmake_minimum_required(VERSION 3.20)
project(BluetoothAudioCli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-agnostic sources.
set(SOURCES
    src/main.cpp
    src/app_controller.cpp
    src/settings.cpp
)

if(WIN32)
    # Windows 10 version 2004 SDK (build 19041) required for AudioPlaybackConnection API.
    # Visual Studio 2019+ with C++/WinRT support is required.
    # In VS 2022, C++/WinRT is built-in.
    # For VS 2019, install the "C++/WinRT" extension via the VS Installer.
    list(APPEND SOURCES
        src/platform_win.cpp
        src/bluetooth_service_win.cpp
        src/audio_service_win.cpp
    )
elseif(UNIX AND NOT APPLE)
    # Linux: BlueZ over D-Bus (sd-bus from libsystemd) + the system audio
    # server (PipeWire / PulseAudio) for the actual A2DP sink stream.
    list(APPEND SOURCES
        src/platform_linux.cpp
        src/bluetooth_service_linux.cpp
        src/audio_service_linux.cpp
    )
else()
    message(FATAL_ERROR "Unsupported platform: only Windows and Linux are supported.")
endif()

add_executable(bluetooth_audio_cli ${SOURCES})

target_include_directories(bluetooth_audio_cli PRIVATE src)

if(WIN32)
    # Windows system libraries (winmm is pulled in via #pragma in platform_win.cpp)
    target_link_libraries(bluetooth_audio_cli PRIVATE
        ole32
        runtimeobject
    )
    # Target Windows 10 2004+ for AudioPlaybackConnection API
    set_target_properties(bluetooth_audio_cli PROPERTIES
        VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0"
    )
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    # libsystemd provides the sd-bus D-Bus client API.
    pkg_check_modules(SDBUS REQUIRED libsystemd)
    find_package(Threads REQUIRED)

    target_include_directories(bluetooth_audio_cli PRIVATE ${SDBUS_INCLUDE_DIRS})
    target_link_libraries(bluetooth_audio_cli PRIVATE
        ${SDBUS_LIBRARIES}
        Threads::Threads
    )
endif()
