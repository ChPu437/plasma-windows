@echo off
setlocal EnableExtensions
rem ===========================================================================
rem session-shell.cmd - session host used as the Windows logon shell
rem
rem This is what the registry value
rem   HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell
rem points at (via cmd.exe /c) after `deploy-vm.cmd install`.
rem
rem It sets up the environment, starts the session bus and services, then
rem runs plasmashell in the foreground. Closing plasmashell (or this
rem window) ends the session, after which the user can log off.
rem
rem Location-independent: package root = this script's folder.
rem ===========================================================================

rem Package root = this script's folder (no hard-coded path).
set "CRAFT_ROOT=%~dp0"
if "%CRAFT_ROOT:~-1%"=="\" set "CRAFT_ROOT=%CRAFT_ROOT:~0,-1%"
set "CRAFT_BIN=%CRAFT_ROOT%\bin"
set "CRAFT_DATA=%CRAFT_ROOT%\bin\data"
set "LOCAL_DATA=%LOCALAPPDATA%"
set "BUS_ADDR=tcp:host=127.0.0.1,port=12443"
set "BUS_PORT=12443"
set "DBUS_CONF=%~dp0dbus-session-plasma.conf"

rem Make the craft-compiled hard-coded prefix (D:/Projects/CraftRoot) work when
rem the package was extracted elsewhere: if it sits in <root>\Projects\CraftRoot,
rem map D: to <root> (virtual drive via subst, nothing on any disk root).
if not exist "D:\Projects\CraftRoot\bin\plasmashell.exe" (
    for %%P in ("%CRAFT_ROOT%\..\..") do set "SUBST_SRC=%%~fP"
    if exist "%SUBST_SRC%\Projects\CraftRoot\bin\plasmashell.exe" (
        subst D: "%SUBST_SRC%" >nul 2>&1
    )
)

if not exist "%CRAFT_BIN%\plasmashell.exe" (
    echo session-shell: %CRAFT_BIN%\plasmashell.exe not found
    cmd /c "start notepad"
    exit /b 1
)

set "PATH=%CRAFT_BIN%;%PATH%"
set "QT_PLUGIN_PATH=%CRAFT_ROOT%\plugins"
set "XDG_CONFIG_HOME=%LOCAL_DATA%"
set "XDG_DATA_HOME=%LOCAL_DATA%"
set "DBUS_SESSION_BUS_ADDRESS=%BUS_ADDR%"
set "QT_QUICK_BACKEND=software"  rem VMware has no GPU; keep rendering reliable

rem Ensure the KDE package data is mirrored (cheap if already done).
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

rem Start the session bus if it is not already listening.
powershell -NoProfile -Command "if (Get-NetTCPConnection -LocalPort %BUS_PORT% -State Listen -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"
if errorlevel 1 (
    if exist "%DBUS_CONF%" (
        start "plasma-dbus" "%CRAFT_BIN%\dbus-daemon.exe" --config-file="%DBUS_CONF%" --nofork
    ) else (
        start "plasma-dbus" "%CRAFT_BIN%\dbus-daemon.exe" --session --nofork
    )
    timeout /t 2 /nobreak >nul
)

start "plasma-activitymanager" "%CRAFT_BIN%\kactivitymanagerd.exe"
start "plasma-kded" "%CRAFT_BIN%\kded6.exe"
timeout /t 3 /nobreak >nul

rem Run the shell in the foreground; keep this script alive as session host.
"%CRAFT_BIN%\plasmashell.exe"
endlocal
