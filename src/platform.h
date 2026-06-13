#pragma once

/// Platform abstraction for process-level concerns that differ between
/// Windows and Linux: runtime/console initialization, performance tuning,
/// and console interrupt (Ctrl+C) handling.
///
/// Keeping these behind a small façade lets main.cpp stay platform-agnostic.
/// Implemented by platform_win.cpp (Windows) and platform_linux.cpp (Linux).
namespace platform {

/// One-time process initialization.
/// Windows: UTF-8 console code pages, WinRT apartment, 1ms timer resolution,
///          high process priority (for glitch-free audio).
/// Linux:   set locale for UTF-8 output.
/// Must be called before constructing services that use the platform runtime.
void Init();

/// Restore anything Init() changed (e.g. timer resolution / priority on Windows).
void Shutdown();

/// Install a handler for console interrupts (Ctrl+C / SIGINT, SIGTERM, close).
/// The handler sets an internal flag observable via ShouldExit().
void InstallSignalHandler();

/// Whether an interrupt has been received and the app should exit gracefully.
bool ShouldExit();

} // namespace platform
