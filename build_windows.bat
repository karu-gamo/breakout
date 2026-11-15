@echo off
setlocal
cd /d "%~dp0"
call :find_cmake
if not defined BREAKOUT_CMAKE (
    echo ERROR: cmake.exe was not found in PATH or standard install locations.
    echo Install CMake with the Add CMake to PATH option, or enable
    echo C++ CMake tools for Windows in Visual Studio Installer.
    echo Then reopen this batch file.
    echo.
    pause
    exit /b 1
)
echo Using CMake: "%BREAKOUT_CMAKE%"
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git is required to download SDL3. Install Git for Windows.
    echo.
    pause
    exit /b 1
)
"%BREAKOUT_CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo ERROR: Configure failed. Install Visual Studio 2022 with Desktop development with C++.
    echo.
    pause
    exit /b 1
)
"%BREAKOUT_CMAKE%" --build build --config Release --parallel
if errorlevel 1 (
    echo ERROR: Build failed.
    echo.
    pause
    exit /b 1
)
echo Build complete: build\bin\breakout.exe
echo.
pause
exit /b 0

:find_cmake
set "BREAKOUT_CMAKE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined BREAKOUT_CMAKE set "BREAKOUT_CMAKE=%%I"
if defined BREAKOUT_CMAKE exit /b 0
if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "BREAKOUT_CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
if defined BREAKOUT_CMAKE exit /b 0
if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set "BREAKOUT_CMAKE=%ProgramFiles(x86)%\CMake\bin\cmake.exe"
if defined BREAKOUT_CMAKE exit /b 0
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" exit /b 0
rem Run from the installer directory to avoid nested cmd quoting of Program Files.
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if errorlevel 1 exit /b 0
for /f "delims=" %%I in ('vswhere.exe -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe') do if not defined BREAKOUT_CMAKE set "BREAKOUT_CMAKE=%%I"
popd
exit /b 0
