@echo off
rem ===========================================================================
rem plasma-common.cmd - shared session bootstrap logic (called via call)
rem
rem Provides labels that all session scripts (start-plasma-session.cmd,
rem session-shell.cmd, plasma-shell.cmd, deploy-vm.cmd) use so environment
rem setup, data mirroring, menu generation, dbus and ksycoca handling are
rem implemented exactly once. ASCII only (no localized text) - callers own
rem their own output.
rem
rem Callers must set CRAFT_ROOT before calling (or call :pc_resolve_root).
rem All labels return via exit /b 0 on success.
rem
rem NOTE: no setlocal here - environment changes (PATH, QT_PLUGIN_PATH, ...)
rem must survive back into the caller.
rem
rem Usage from another batch file:
rem   call "%~dp0plasma-common.cmd" :pc_setup_env
rem   call "%~dp0plasma-common.cmd" :pc_mirror_data
rem ===========================================================================

goto %~1 2>nul
goto :eof

rem ---------------------------------------------------------------------------
rem :pc_resolve_root - set CRAFT_ROOT to this script's folder
rem ---------------------------------------------------------------------------
:pc_resolve_root
if "%CRAFT_ROOT%"=="" set "CRAFT_ROOT=%~dp0"
if "%CRAFT_ROOT:~-1%"=="\" set "CRAFT_ROOT=%CRAFT_ROOT:~0,-1%"
set "CRAFT_BIN=%CRAFT_ROOT%\bin"
set "CRAFT_DATA=%CRAFT_ROOT%\bin\data"
exit /b 0

rem ---------------------------------------------------------------------------
rem :pc_setup_env - PATH, Qt plugins, XDG dirs, DBus address, software backend
rem ---------------------------------------------------------------------------
:pc_setup_env
call :pc_resolve_root
set "PATH=%CRAFT_BIN%;%PATH%"
set "QT_PLUGIN_PATH=%CRAFT_ROOT%\plugins"
set "XDG_CONFIG_HOME=%LOCALAPPDATA%"
set "XDG_DATA_HOME=%LOCALAPPDATA%"
set "DBUS_SESSION_BUS_ADDRESS=tcp:host=127.0.0.1,port=12443"
set "QT_QUICK_BACKEND=software"
exit /b 0

rem ---------------------------------------------------------------------------
rem :pc_mirror_data - mirror KDE package data to %LOCALAPPDATA%
rem (QStandardPaths on Windows resolves GenericDataLocation to LOCALAPPDATA)
rem ---------------------------------------------------------------------------
:pc_mirror_data
call :pc_resolve_root
for %%S in (plasmoids shells wallpapers layout-templates desktoptheme look-and-feel) do (
    if exist "%CRAFT_DATA%\plasma\%%S" (
        if not exist "%LOCALAPPDATA%\plasma\%%S" mkdir "%LOCALAPPDATA%\plasma\%%S"
        for /d %%D in ("%CRAFT_DATA%\plasma\%%S\*") do (
            xcopy /e /i /y "%%D" "%LOCALAPPDATA%\plasma\%%S\%%~nxD" >nul
        )
    )
)
if not exist "%LOCALAPPDATA%\applications" mkdir "%LOCALAPPDATA%\applications"
copy /y "%CRAFT_DATA%\applications\*.desktop" "%LOCALAPPDATA%\applications\" >nul 2>&1
exit /b 0

rem ---------------------------------------------------------------------------
rem :pc_write_menu - write %LOCALAPPDATA%\menus\applications.menu
rem ---------------------------------------------------------------------------
:pc_write_menu
call :pc_resolve_root
if not exist "%LOCALAPPDATA%\menus" mkdir "%LOCALAPPDATA%\menus"
(
    echo ^<!DOCTYPE Menu PUBLIC "-//freedesktop//DTD Menu 1.0//EN"
    echo   "http://www.freedesktop.org/standards/menu-spec/menu-1.0.dtd"^>
    echo ^<Menu^>
    echo   ^<Name^>Applications^</Name^>
    echo   ^<Directory^>applications.directory^</Directory^>
    echo   ^<AppDir^>%LOCALAPPDATA:\=/%/applications^</AppDir^>
    echo   ^<AppDir^>%CRAFT_DATA:\=/%/applications^</AppDir^>
    echo   ^<DefaultDirectoryDirs/^>
    echo   ^<DefaultMergeDirs/^>
    echo   ^<Include^>
    echo     ^<All/^>
    echo   ^</Include^>
    echo ^</Menu^>
) > "%LOCALAPPDATA%\menus\applications.menu"
exit /b 0

rem ---------------------------------------------------------------------------
rem :pc_rebuild_ksycoca - rebuild the KService database
rem ---------------------------------------------------------------------------
:pc_rebuild_ksycoca
call :pc_resolve_root
"%CRAFT_BIN%\kbuildsycoca6.exe" --noincremental >nul 2>&1
exit /b 0

rem ---------------------------------------------------------------------------
rem :pc_start_bus - start the session bus if not already listening (port 12443)
rem ---------------------------------------------------------------------------
:pc_start_bus
call :pc_resolve_root
set "DBUS_CONF=%~dp0dbus-session-plasma.conf"
powershell -NoProfile -Command "if (Get-NetTCPConnection -LocalPort 12443 -State Listen -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"
if errorlevel 1 (
    if exist "%DBUS_CONF%" (
        start "plasma-dbus" "%CRAFT_BIN%\dbus-daemon.exe" --config-file="%DBUS_CONF%" --nofork
    ) else (
        start "plasma-dbus" "%CRAFT_BIN%\dbus-daemon.exe" --session --nofork
    )
    timeout /t 2 /nobreak >nul
)
exit /b 0

rem ---------------------------------------------------------------------------
rem :pc_start_services - kactivitymanagerd + kded6 + trayhost in the background
rem ---------------------------------------------------------------------------
:pc_start_services
call :pc_resolve_root
start "plasma-activitymanager" "%CRAFT_BIN%\kactivitymanagerd.exe"
start "plasma-kded" "%CRAFT_BIN%\kded6.exe"
rem Windows tray host (SNI bridge into the Plasma system tray); its log
rem lands next to the exe (working dir), i.e. %CRAFT_BIN%\trayhost.log.
if exist "%CRAFT_BIN%\trayhost.exe" (
    start "plasma-trayhost" /D "%CRAFT_BIN%" "%CRAFT_BIN%\trayhost.exe"
)
timeout /t 3 /nobreak >nul
exit /b 0
