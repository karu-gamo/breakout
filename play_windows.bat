@echo off
setlocal
cd /d "%~dp0"
if not exist "build\bin\breakout.exe" (
    echo ERROR: Game not found. Run build_windows.bat first.
    exit /b 1
)
start "" /wait "build\bin\breakout.exe"
exit /b %errorlevel%
