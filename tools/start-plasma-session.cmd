@echo off
setlocal EnableExtensions
rem ===========================================================================
rem start-plasma-session.cmd - Plasma session bootstrap on Windows
rem
rem Starts:
rem   1. the session bus (dbus-daemon, fixed address from tools\dbus\session-plasma.conf)
rem   2. kactivitymanagerd (activity service) in the background
rem   3. kded6 (KDE service daemon) in the foreground
rem
rem Also prepares the KDE runtime data on first use:
rem   - mirrors craft-installed package data (plasmoids, shells, wallpapers,
rem     layout-templates, desktoptheme, look-and-feel) and .desktop files
rem     from CraftRoot\bin\data to %LOCALAPPDATA% (QStandardPaths on
rem     Windows uses LOCALAPPDATA, not the craft install prefix)
rem   - writes %LOCALAPPDATA%\menus\applications.menu (on Windows KService's
rem     DefaultAppDirs points at the Start Menu, so AppDir is set explicitly)
rem   - rebuilds the ksycoca service database so kickoff etc. can list apps
rem   - generates the mime database if missing
rem
rem Clients must use: DBUS_SESSION_BUS_ADDRESS=tcp:host=127.0.0.1,port=12443
rem (exported by this script for everything it launches).
rem ===========================================================================

set "BUS_ADDR=tcp:host=127.0.0.1,port=12443"
set "BUS_PORT=12443"
set "CRAFT_ROOT=D:\Projects\CraftRoot"
set "CRAFT_BIN=%CRAFT_ROOT%\bin"
set "CRAFT_DATA=%CRAFT_ROOT%\bin\data"
set "DBUS_CONF=%~dp0dbus\session-plasma.conf"
set "LOCAL_DATA=%LOCALAPPDATA%"

rem 0. Mirror KDE package data into %LOCALAPPDATA% (QStandardPaths on Windows).
echo Preparing KDE runtime data in %LOCAL_DATA%...
for %%S in (plasmoids shells wallpapers layout-templates desktoptheme look-and-feel) do (
    if exist "%CRAFT_DATA%\plasma\%%S" (
        if not exist "%LOCAL_DATA%\plasma\%%S" mkdir "%LOCAL_DATA%\plasma\%%S"
        for /d %%D in ("%CRAFT_DATA%\plasma\%%S\*") do (
            xcopy /e /i /y "%%D" "%LOCAL_DATA%\plasma\%%S\%%~nxD" >nul
        )
    )
)
if not exist "%LOCAL_DATA%\applications" mkdir "%LOCAL_DATA%\applications"
copy /y "%CRAFT_DATA%\applications\*.desktop" "%LOCAL_DATA%\applications\" >nul 2>&1

rem Write applications.menu (KService DefaultAppDirs points at the Start
rem Menu on Windows, so point AppDir at the KDE .desktop dirs explicitly).
if not exist "%LOCAL_DATA%\menus" mkdir "%LOCAL_DATA%\menus"
(
    echo ^<!DOCTYPE Menu PUBLIC "-//freedesktop//DTD Menu 1.0//EN"
    echo   "http://www.freedesktop.org/standards/menu-spec/menu-1.0.dtd"^>
    echo ^<Menu^>
    echo   ^<Name^>Applications^</Name^>
    echo   ^<Directory^>applications.directory^</Directory^>
    echo   ^<AppDir^>%LOCAL_DATA:\=/%/applications^</AppDir^>
    echo   ^<AppDir^>%CRAFT_DATA:\=/%/applications^</AppDir^>
    echo   ^<DefaultDirectoryDirs/^>
    echo   ^<DefaultMergeDirs/^>
    echo   ^<Include^>
    echo     ^<All/^>
    echo   ^</Include^>
    echo ^</Menu^>
) > "%LOCAL_DATA%\menus\applications.menu"

rem Generate .desktop bridges for the Windows Start Menu (native apps in kickoff).
powershell -ExecutionPolicy Bypass -File "%~dp0build-startmenu-desktops.ps1" >nul 2>&1

rem Rebuild the service database (ksycoca) so kickoff can list applications.
"%CRAFT_BIN%\kbuildsycoca6.exe" --noincremental >nul 2>&1

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
echo Starting kded6...  (logs: %LOCAL_DATA%\plasma-win-session\kded6.log)
if not exist "%LOCAL_DATA%\plasma-win-session" mkdir "%LOCAL_DATA%\plasma-win-session"
"%CRAFT_BIN%\kded6.exe" > "%LOCAL_DATA%\plasma-win-session\kded6.log" 2>&1
echo kded6 exited with code %errorlevel%. See %LOCAL_DATA%\plasma-win-session\kded6.log
endlocal
