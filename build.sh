#!/usr/bin/env bash
set -euo pipefail

echo "============================================"
echo " Bluetooth Audio Receiver CLI - Build Script"
echo "============================================"
echo
echo "Build requirements:"
echo "  - C++17 compiler (g++ / clang++), CMake >= 3.20, pkg-config"
echo "  - libsystemd development headers (sd-bus):"
echo "      Debian/Ubuntu : sudo apt install build-essential cmake pkg-config libsystemd-dev"
echo "      Fedora        : sudo dnf install gcc-c++ cmake pkgconf-pkg-config systemd-devel"
echo "      Arch          : sudo pacman -S base-devel cmake systemd"
echo
echo "Runtime requirements:"
echo "  - BlueZ (bluetoothd) and a paired audio source device"
echo "  - PipeWire (with libspa-0.2-bluetooth) or PulseAudio configured for A2DP sink"
echo

BUILD_DIR="${1:-build}"

echo "Configuring (build dir: ${BUILD_DIR})..."
cmake -B "${BUILD_DIR}" -S . -DCMAKE_BUILD_TYPE=Release

echo
echo "Building..."
cmake --build "${BUILD_DIR}" -j "$(nproc 2>/dev/null || echo 2)"

echo
echo "============================================"
echo " Build successful!"
echo " Output: ${BUILD_DIR}/bluetooth_audio_cli"
echo "============================================"
echo
echo "Run with: ${BUILD_DIR}/bluetooth_audio_cli"
