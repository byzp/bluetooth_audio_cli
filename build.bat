@echo off
echo ============================================
echo  Bluetooth Audio Receiver CLI - Build Script
echo ============================================
echo.
echo Requirements:
echo   - Visual Studio 2019+ with C++/WinRT
echo   - Windows SDK 10.0.19041.0+
echo.

REM Detect Visual Studio
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

REM Configure with CMake
echo Configuring...
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo CMake configuration failed.
    echo If you have VS 2019, try: cmake -B build -S . -G "Visual Studio 16 2019" -A x64
    exit /b 1
)

REM Build
echo.
echo Building...
cmake --build build --config Release
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
