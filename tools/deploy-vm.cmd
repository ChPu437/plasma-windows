@echo off
setlocal EnableExtensions
rem ===========================================================================
rem deploy-vm.cmd - one-shot deployment + session start inside the test VM
rem
rem Location-independent: the CraftRoot runtime must sit in the same folder
rem as this script (extract plasma-vm.zip next to it, or copy the whole
rem package). There is no hard-coded drive/path.
rem
rem Usage:
rem   deploy-vm.cmd             start the session (dbus + kactivitymanagerd
rem                             + kded6 + plasmashell) without touching the
rem                             shell configuration
rem   deploy-vm.cmd install     switch the current user's default shell to
rem                             plasmashell.exe (backup first), then start
rem                             the session
rem   deploy-vm.cmd restore     restore the previous default shell and exit
rem
rem The script:
rem   1. sets up the environment (PATH, Qt plugins, XDG dirs)
rem   2. mirrors KDE package data (plasmoids, shells, wallpapers, .desktop
rem      files, ...) from bin\data to %LOCALAPPDATA%
rem      (QStandardPaths on Windows resolves GenericDataLocation to
rem      LOCALAPPDATA, not the package prefix)
rem   3. writes %LOCALAPPDATA%\menus\applications.menu (KService's
rem      DefaultAppDirs points at the Start Menu on Windows, so AppDir is
rem      set explicitly)
rem   4. rebuilds the ksycoca service database (kickoff app listing)
rem   5. (optional) switches the user shell to plasmashell
rem   6. starts the session: dbus-daemon + kactivitymanagerd + kded6
rem      + plasmashell
rem
rem Only the current user's Winlogon\Shell value is touched; system
rem binaries and Winlogon itself are never modified. The previous value
rem is saved to %USERPROFILE%\.plasma-windows\shell-backup.txt.
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
set "SHELL_KEY=HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon"
set "SHELL_BACKUP=%USERPROFILE%\.plasma-windows\shell-backup.txt"
set "PLASMA_SHELL=%CRAFT_BIN%\plasmashell.exe"
set "SESSION_LAUNCHER=%CRAFT_ROOT%\session-shell.cmd"
set "ACTION=%~1"

if /i "%ACTION%"=="restore" goto :restore_shell

if not exist "%CRAFT_BIN%\plasmashell.exe" (
    echo ERROR: %CRAFT_BIN%\plasmashell.exe not found.
    echo The CraftRoot runtime must sit in the same folder as this script.
    echo Extract plasma-vm.zip next to deploy-vm.cmd first.
    pause
    exit /b 1
)

if /i "%ACTION%"=="install" goto :install_shell

:session
echo === 1/5 Environment ===
set "PATH=%CRAFT_BIN%;%PATH%"
set "QT_PLUGIN_PATH=%CRAFT_ROOT%\plugins"
set "XDG_CONFIG_HOME=%LOCAL_DATA%"
set "XDG_DATA_HOME=%LOCAL_DATA%"
set "DBUS_SESSION_BUS_ADDRESS=%BUS_ADDR%"
set "QT_QUICK_BACKEND=software"  rem VMware has no GPU; keep rendering reliable

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
    echo WARNING: dbus config not found next to script [%DBUS_CONF%], using defaults.
    set "DBUS_CONF="
)
if defined DBUS_CONF (
    "%CRAFT_BIN%\dbus-daemon.exe" --config-file="%DBUS_CONF%" --nofork >nul 2>&1
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
exit /b %errorlevel%

rem ---------------------------------------------------------------------------
rem install: back up the current user shell and switch it to plasmashell.
rem ---------------------------------------------------------------------------
:install_shell
echo === Switching user shell to plasmashell ===
if not exist "%USERPROFILE%\.plasma-windows" mkdir "%USERPROFILE%\.plasma-windows"

rem Back up the current value (write it raw so restore is exact).
set "CURRENT_SHELL="
for /f "usebackq skip=2 tokens=1,* delims= " %%A in (`reg query "%SHELL_KEY%" /v Shell 2^>nul`) do (
    if "%%A"=="Shell" set "CURRENT_SHELL=%%B"
)
if defined CURRENT_SHELL (
    > "%SHELL_BACKUP%" echo %CURRENT_SHELL%
    echo Backed up current shell: %CURRENT_SHELL%
) else (
    > "%SHELL_BACKUP%" echo explorer.exe
    echo No previous shell value found; will restore explorer.exe on rollback.
)

rem Point the shell at the session launcher (starts dbus + services +
rem plasmashell), so a plain logon gets a working plasma session.
if exist "%SESSION_LAUNCHER%" (
    reg add "%SHELL_KEY%" /v Shell /t REG_SZ /d "cmd.exe /c \"%SESSION_LAUNCHER%\"" /f >nul
) else (
    echo WARNING: %SESSION_LAUNCHER% not found; using plain plasmashell.exe.
    reg add "%SHELL_KEY%" /v Shell /t REG_SZ /d "%PLASMA_SHELL%" /f >nul
)
if errorlevel 1 (
    echo ERROR: failed to write the shell registry value.
    pause
    exit /b 1
)
echo User shell is now: cmd.exe /c "%SESSION_LAUNCHER%"
echo Takes effect at next logon/restart. Starting the session now for testing...
goto :session

rem ---------------------------------------------------------------------------
rem restore: put the backed-up shell value back (explorer.exe if no backup).
rem ---------------------------------------------------------------------------
:restore_shell
echo === Restoring previous user shell ===
if exist "%SHELL_BACKUP%" (
    set /p OLD_SHELL=<"%SHELL_BACKUP%"
    if not defined OLD_SHELL set "OLD_SHELL=explorer.exe"
) else (
    set "OLD_SHELL=explorer.exe"
)
reg add "%SHELL_KEY%" /v Shell /t REG_SZ /d "%OLD_SHELL%" /f >nul
echo User shell restored to: %OLD_SHELL%
echo Takes effect at next logon/restart.
endlocal
exit /b 0

