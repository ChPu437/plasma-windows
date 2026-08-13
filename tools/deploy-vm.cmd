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
rem   1. sets up the environment (see plasma-common.cmd)
rem   2. mirrors KDE package data to %LOCALAPPDATA%
rem   3. writes %LOCALAPPDATA%\menus\applications.menu
rem   4. rebuilds the ksycoca service database
rem   5. (optional) switches the user shell to plasmashell
rem   6. starts the session: dbus-daemon + kactivitymanagerd + kded6
rem      + plasmashell
rem
rem Only the current user's Winlogon\Shell value is touched; system
rem binaries and Winlogon itself are never modified. The previous value
rem is saved to %USERPROFILE%\.plasma-windows\shell-backup.txt.
rem ===========================================================================

rem Package root = this script's folder (no hard-coded path).
call "%~dp0plasma-common.cmd" :pc_resolve_root
set "SHELL_KEY=HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon"
set "SHELL_BACKUP=%USERPROFILE%\.plasma-windows\shell-backup.txt"
set "PLASMA_SHELL=%CRAFT_BIN%\plasmashell.exe"
set "SESSION_LAUNCHER=%CRAFT_ROOT%\session-shell.cmd"
set "ACTION=%~1"

if /i "%ACTION%"=="restore" goto :restore_shell

rem Make the craft-compiled hard-coded prefix (D:/Projects/CraftRoot) work when
rem the package was extracted elsewhere: if it sits in <root>\Projects\CraftRoot,
rem map D: to <root> (virtual drive via subst, nothing on any disk root).
if not exist "D:\Projects\CraftRoot\bin\plasmashell.exe" (
    for %%P in ("%CRAFT_ROOT%\..\..") do set "SUBST_SRC=%%~fP"
    if exist "%SUBST_SRC%\Projects\CraftRoot\bin\plasmashell.exe" (
        subst D: "%SUBST_SRC%" >nul 2>&1
    ) else (
        echo WARNING: package is not under a "Projects\CraftRoot" layout.
        echo craft-compiled tools reference D:/Projects/CraftRoot and will fail.
        echo For full support extract plasma-vm.zip to  ^<any^>\Projects\CraftRoot
    )
)

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
call "%~dp0plasma-common.cmd" :pc_setup_env

echo === 2/5 Mirror KDE package data to %%LOCALAPPDATA%% ===
call "%~dp0plasma-common.cmd" :pc_mirror_data

echo === 3/5 applications.menu ===
call "%~dp0plasma-common.cmd" :pc_write_menu

echo === 4/5 Rebuild ksycoca ===
call "%~dp0plasma-common.cmd" :pc_rebuild_ksycoca

echo === 5/5 Start session (dbus + kactivitymanagerd + kded6 + plasmashell) ===
call "%~dp0plasma-common.cmd" :pc_start_bus
call "%~dp0plasma-common.cmd" :pc_start_services

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
