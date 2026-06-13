@echo off
setlocal enabledelayedexpansion

echo ============================================
echo  Bluetooth Audio Receiver CLI - Build Script
echo ============================================
echo.
echo Requirements:
echo   - Visual Studio 2026 with C++/WinRT
echo   - Windows SDK 10.0.19041.0+
echo.

REM Detect Visual Studio via vswhere
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        set VS_PATH=%%i
    )
) else (
    echo vswhere.exe not found. Is Visual Studio installed?
    exit /b 1
)

if not defined VS_PATH (
    echo No Visual Studio installation found.
    exit /b 1
)

echo Using Visual Studio: %VS_PATH%
echo.

REM Set up the MSVC build environment from the detected VS path
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if %ERRORLEVEL% NEQ 0 (
    echo Failed to set up Visual Studio environment.
    exit /b 1
)

REM Use Visual Studio 18 2026 generator with VS-bundled CMake 4.2+
REM The VS-bundled CMake can detect this VS instance even at non-standard paths.
set CMAKE="%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

echo Configuring...
%CMAKE% -B build -S . -G "Visual Studio 18 2026" -A x64 --fresh
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo CMake configuration failed.
    exit /b 1
)

REM Build
echo.
echo Building...
%CMAKE% --build build --config Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed.
    exit /b 1
)

echo.
echo ============================================
echo  Build successful!
echo  Output: build\Release\bluetooth_audio_cli.exe
echo ============================================
echo.
echo Run with: build\Release\bluetooth_audio_cli.exe
