@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake is required. Install CMake and add it to PATH.
    exit /b 1
)
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git is required to download SDL3. Install Git for Windows.
    exit /b 1
)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo ERROR: Configure failed. Install Visual Studio 2022 with Desktop development with C++.
    exit /b 1
)
cmake --build build --config Release --parallel
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)
echo Build complete: build\bin\breakout.exe
exit /b 0
