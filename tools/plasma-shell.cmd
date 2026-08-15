@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Plasma Shell Session Manager (VM)

rem ===========================================================================
rem plasma-shell.cmd - interactive Plasma session manager, run inside the VM.
rem
rem Usage: double-click or run from a cmd window. Menu:
rem
rem   1. Start a temporary Plasma session (no system changes)
rem   2. Set Plasma as the default shell (backup first, effective on next
rem      login; restore via option 3)
rem   3. Restore Explorer as the default shell (rollback)
rem   4. Show current status
rem   0. Exit
rem
rem Only touches the current user's HKCU\...\Winlogon\Shell value - never
rem system-wide settings or Winlogon itself. The bus / services / menu /
rem dbus / ksycoca logic lives in plasma-common.cmd in the same directory.
rem ===========================================================================

rem -------- locate CraftRoot (this script lives next to bin/) --------
set "CRAFT_ROOT=%~dp0"
if "%CRAFT_ROOT:~-1%"=="\" set "CRAFT_ROOT=%CRAFT_ROOT:~0,-1%"
set "CRAFT_BIN=%CRAFT_ROOT%\bin"
set "SHELL_KEY=HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon"
set "BACKUP_DIR=%USERPROFILE%\.plasma-windows"
set "SHELL_BACKUP=%BACKUP_DIR%\shell-backup.txt"
set "SESSION_LAUNCHER=%CRAFT_ROOT%\session-shell.cmd"

rem -------- sanity: plasmashell present --------
if not exist "%CRAFT_BIN%\plasmashell.exe" (
    echo.
    echo [ERROR] %CRAFT_BIN%\plasmashell.exe not found.
    echo         Make sure this script sits inside the CraftRoot directory,
    echo         next to the bin folder.
    pause
    exit /b 1
)

goto :menu

rem ---------------------------------------------------------------------------
rem :env_setup - session env + mirror KDE data into %LOCALAPPDATA%
rem ---------------------------------------------------------------------------
:env_setup
call "%~dp0plasma-common.cmd" :pc_setup_env
echo [PREP] Mirroring KDE runtime data to %LOCALAPPDATA% ...
call "%~dp0plasma-common.cmd" :pc_mirror_data
call "%~dp0plasma-common.cmd" :pc_write_menu
echo [PREP] Rebuilding ksycoca database ...
call "%~dp0plasma-common.cmd" :pc_rebuild_ksycoca
exit /b 0

rem ---------------------------------------------------------------------------
rem :start_session - dbus + services + plasmashell in the foreground
rem ---------------------------------------------------------------------------
:start_session
echo.
echo [SESSION] Starting session bus ...
call "%~dp0plasma-common.cmd" :pc_start_bus
echo [SESSION] Starting kactivitymanagerd / kded6 ...
call "%~dp0plasma-common.cmd" :pc_start_services
echo [SESSION] Starting plasmashell (close this window to end the session)...
"%CRAFT_BIN%\plasmashell.exe"
echo [SESSION] plasmashell exited with code %errorlevel%
echo.
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :install - set Plasma as the default shell for this user
rem ---------------------------------------------------------------------------
:install
echo.
echo [INSTALL] Switching the current user's default Shell to Plasma ...
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"

set "CURRENT_SHELL="
for /f "usebackq skip=2 tokens=1,* delims= " %%A in (`reg query "%SHELL_KEY%" /v Shell 2^>nul`) do (
    if "%%A"=="Shell" set "CURRENT_SHELL=%%B"
)
if defined CURRENT_SHELL (
    > "%SHELL_BACKUP%" echo %CURRENT_SHELL%
    echo           Backed up current Shell: %CURRENT_SHELL%
) else (
    > "%SHELL_BACKUP%" echo explorer.exe
    echo           No previous Shell value found - restore will use explorer.exe
)

reg add "%SHELL_KEY%" /v Shell /t REG_SZ /d "cmd.exe /c \"%SESSION_LAUNCHER%\"" /f >nul
if errorlevel 1 (
    echo [ERROR] Failed to write the registry value.
    pause
    exit /b 1
)
echo           Installed: cmd.exe /c "%SESSION_LAUNCHER%"
echo           Effective at the next logon.
echo.
echo           To roll back: run this script and pick [3].
echo.
set /p GO=Start a temporary Plasma session now? (Y/N):
if /i "%GO%"=="Y" (
    call :env_setup
    call :start_session
)
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :restore - restore the backed-up shell value
rem ---------------------------------------------------------------------------
:restore
echo.
echo [RESTORE] Restoring the default Shell ...
set "OLD_SHELL=explorer.exe"
if exist "%SHELL_BACKUP%" (
    set /p OLD_SHELL=<"%SHELL_BACKUP%"
    if not defined OLD_SHELL set "OLD_SHELL=explorer.exe"
)
reg add "%SHELL_KEY%" /v Shell /t REG_SZ /d "%OLD_SHELL%" /f >nul
if errorlevel 1 (
    echo [ERROR] Failed to write the registry value.
    pause
    exit /b 1
)
echo           Restored: %OLD_SHELL%
echo           Effective at the next logon.
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :status - show the current shell setting and backup
rem ---------------------------------------------------------------------------
:status
echo.
echo [STATUS] Registry key: %SHELL_KEY%
echo.
echo           Current Shell value:
reg query "%SHELL_KEY%" /v Shell 2>nul
if errorlevel 1 echo           (not set - using the system default explorer.exe)
echo.
if exist "%SHELL_BACKUP%" (
    echo           Backup file: %SHELL_BACKUP%
    set /p PREV=<"%SHELL_BACKUP%"
    echo           Backed-up value: !PREV!
) else (
    echo           Backup file: %SHELL_BACKUP%  (no backup yet)
)
echo.
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :menu - main menu
rem ---------------------------------------------------------------------------
:menu
cls
echo.
echo  ============================================
echo      Plasma Shell Session Manager (VM)
echo  ============================================
echo.
echo    CraftRoot: %CRAFT_ROOT%
echo.
echo    [1] Start a temporary Plasma session (no config changes)
echo    [2] Set Plasma as the default shell (backup, next logon)
echo    [3] Restore Explorer as the default shell (rollback)
echo    [4] Show current status
echo    [0] Exit
echo.
set /p CHOICE=Your choice:

if "%CHOICE%"=="1" (
    call :env_setup
    call :start_session
    goto :menu
)
if "%CHOICE%"=="2" goto :install
if "%CHOICE%"=="3" goto :restore
if "%CHOICE%"=="4" goto :status
if "%CHOICE%"=="0" (
    endlocal
    exit /b 0
)
echo.
echo  Invalid choice - please try again.
timeout /t 2 /nobreak >nul
goto :menu
