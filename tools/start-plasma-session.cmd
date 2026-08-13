@echo off
setlocal EnableExtensions
rem ===========================================================================
rem start-plasma-session.cmd - Plasma session bootstrap on Windows (dev box)
rem
rem Starts the session bus, kactivitymanagerd and kded6, then runs
rem plasmashell in the foreground. Also prepares the KDE runtime data on
rem first use (mirror + applications.menu + ksycoca) - see plasma-common.cmd
rem for the shared implementation.
rem
rem Clients must use: DBUS_SESSION_BUS_ADDRESS=tcp:host=127.0.0.1,port=12443
rem (exported by pc_setup_env for everything this script launches).
rem ===========================================================================

set "CRAFT_ROOT=D:\Projects\CraftRoot"

call "%~dp0plasma-common.cmd" :pc_setup_env
if errorlevel 1 goto :fail

echo Preparing KDE runtime data in %LOCALAPPDATA%...
call "%~dp0plasma-common.cmd" :pc_mirror_data
call "%~dp0plasma-common.cmd" :pc_write_menu

rem Generate .desktop bridges for the Windows Start Menu (native apps in kickoff).
powershell -ExecutionPolicy Bypass -File "%~dp0build-startmenu-desktops.ps1" >nul 2>&1

call "%~dp0plasma-common.cmd" :pc_rebuild_ksycoca

call "%~dp0plasma-common.cmd" :pc_start_bus
call "%~dp0plasma-common.cmd" :pc_start_services

rem Run kded6 in the foreground; keep this script alive as session host.
echo Starting kded6...  (logs: %LOCALAPPDATA%\plasma-win-session\kded6.log)
if not exist "%LOCALAPPDATA%\plasma-win-session" mkdir "%LOCALAPPDATA%\plasma-win-session"
"%CRAFT_BIN%\kded6.exe" > "%LOCALAPPDATA%\plasma-win-session\kded6.log" 2>&1
echo kded6 exited with code %errorlevel%. See %LOCALAPPDATA%\plasma-win-session\kded6.log
exit /b %errorlevel%

:fail
echo start-plasma-session: setup failed.
exit /b 1
