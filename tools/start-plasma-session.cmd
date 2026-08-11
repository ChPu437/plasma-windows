@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ===========================================================================
rem start-plasma-session.cmd - Phase 3 M1: Plasma session bootstrap on Windows
rem
rem Starts:
rem   1. the session bus (dbus-daemon, fixed address from tools\dbus\session-plasma.conf)
rem   2. kded6 (KDE service daemon) in the foreground
rem
rem Clients must use: DBUS_SESSION_BUS_ADDRESS=tcp:host=127.0.0.1,port=12443
rem (exported by this script for everything it launches).
rem ===========================================================================

set "BUS_ADDR=tcp:host=127.0.0.1,port=12443"
set "BUS_PORT=12443"
set "CRAFT_BIN=D:\Projects\CraftRoot\bin"
set "DBUS_CONF=%~dp0dbus\session-plasma.conf"

rem 1. Start the session bus if it is not listening yet.
powershell -NoProfile -Command "if (Get-NetTCPConnection -LocalPort %BUS_PORT% -State Listen -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"
if errorlevel 1 (
    echo Starting session bus on %BUS_ADDR%
    powershell -NoProfile -Command "Start-Process -FilePath '%CRAFT_BIN%\dbus-daemon.exe' -ArgumentList '--config-file=%DBUS_CONF%','--nofork' -WindowStyle Hidden"
    :waitbus
    powershell -NoProfile -Command "if (Get-NetTCPConnection -LocalPort %BUS_PORT% -State Listen -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"
    if errorlevel 1 (
        timeout /t 1 /nobreak >nul
        goto :waitbus
    )
) else (
    echo Session bus already running on %BUS_ADDR%
)

rem 2. Export the address (quoted set: no trailing space in the value).
set "DBUS_SESSION_BUS_ADDRESS=%BUS_ADDR%"

rem 3. Start kactivitymanagerd in the background.
powershell -NoProfile -Command "Start-Process -FilePath '%CRAFT_BIN%\kactivitymanagerd.exe' -WindowStyle Hidden"

rem 4. Start kded6 in the foreground; keep this script alive as session host.
echo Starting kded6...
"%CRAFT_BIN%\kded6.exe"
echo kded6 exited with code %errorlevel%
endlocal
