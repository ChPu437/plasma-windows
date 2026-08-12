@echo off
setlocal EnableExtensions
rem ===========================================================================
rem deploy-vm.cmd - one-shot deployment + session start inside the test VM
rem
rem Assumes the CraftRoot runtime was extracted to D:\Projects\CraftRoot
rem (from plasma-vm.7z).  This script:
rem   1. sets up the environment (PATH, Qt plugins, XDG dirs)
rem   2. mirrors KDE package data (plasmoids, shells, wallpapers, .desktop
rem      files, ...) from CraftRoot\bin\data to %LOCALAPPDATA%
rem      (QStandardPaths on Windows resolves GenericDataLocation to
rem      LOCALAPPDATA, not the craft prefix)
rem   3. writes %LOCALAPPDATA%\menus\applications.menu (KService's
rem      DefaultAppDirs points at the Start Menu on Windows, so AppDir is
rem      set explicitly)
rem   4. rebuilds the ksycoca service database (kickoff app listing)
rem   5. starts the session: dbus-daemon + kactivitymanagerd + kded6
rem      + plasmashell
rem
rem Run this from the VM as a normal user. plasmashell runs in the
rem foreground so the script stays alive as the session host; close it
rem (or plasmashell) to end the session.
rem ===========================================================================

set "CRAFT_ROOT=D:\Projects\CraftRoot"
set "CRAFT_BIN=%CRAFT_ROOT%\bin"
set "CRAFT_DATA=%CRAFT_ROOT%\bin\data"
set "LOCAL_DATA=%LOCALAPPDATA%"
set "BUS_ADDR=tcp:host=127.0.0.1,port=12443"
set "BUS_PORT=12443"
set "DBUS_CONF=%~dp0dbus-session-plasma.conf"

if not exist "%CRAFT_BIN%\plasmashell.exe" (
    echo ERROR: %CRAFT_BIN%\plasmashell.exe not found.
    echo Extract plasma-vm.7z to %CRAFT_ROOT% first, e.g.:
    echo    7z x plasma-vm.7z -oD:\Projects\CraftRoot
    pause
    exit /b 1
)

echo === 1/5 Environment ===
set "PATH=%CRAFT_BIN%;%PATH%"
set "QT_PLUGIN_PATH=%CRAFT_ROOT%\plugins"
set "XDG_CONFIG_HOME=%LOCAL_DATA%"
set "XDG_DATA_HOME=%LOCAL_DATA%"
set "DBUS_SESSION_BUS_ADDRESS=%BUS_ADDR%"

echo === 2/5 Mirror KDE package data to %%LOCALAPPDATA%% ===
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

echo === 3/5 applications.menu ===
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

echo === 4/5 Rebuild ksycoca ===
"%CRAFT_BIN%\kbuildsycoca6.exe" --noincremental >nul 2>&1

echo === 5/5 Start session (dbus + kactivitymanagerd + kded6 + plasmashell) ===
if not exist "%DBUS_CONF%" (
    echo WARNING: dbus config not found next to script (%DBUS_CONF%); using defaults.
    set "DBUS_CONF="
)
if defined DBUS_CONF (
    "%CRAFT_BIN%\dbus-daemon.exe" --config-file=%DBUS_CONF% --nofork >nul 2>&1
) else (
    start "plasma-dbus" "%CRAFT_BIN%\dbus-daemon.exe" --session --nofork
)
timeout /t 2 /nobreak >nul
start "plasma-activitymanager" "%CRAFT_BIN%\kactivitymanagerd.exe"
start "plasma-kded" "%CRAFT_BIN%\kded6.exe"
timeout /t 3 /nobreak >nul

echo Starting plasmashell (close this window to end the session)...
"%CRAFT_BIN%\plasmashell.exe"
echo plasmashell exited with code %errorlevel%
endlocal
