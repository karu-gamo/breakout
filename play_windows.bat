@echo off
setlocal
cd /d "%~dp0"
if not exist "build\bin\breakout.exe" (
    echo ERROR: Game not found. Run build_windows.bat first.
    echo.
    pause
    exit /b 1
)
"build\bin\breakout.exe"
set "game_exit_code=%errorlevel%"
if not "%game_exit_code%"=="0" (
    echo ERROR: Game exited with code %game_exit_code%.
    echo.
    pause
)
exit /b %game_exit_code%
