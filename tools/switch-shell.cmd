@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Plasma Windows - shell switcher (Phase 0.5)

rem ===========================================================================
rem switch-shell.cmd - Phase 0.5 shell-switching mechanism (see AGENTS.md)
rem
rem Changes ONLY the current user's shell configuration via
rem   HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell
rem It never modifies system binaries and never touches Winlogon itself.
rem
rem Usage:
rem   switch-shell.cmd status
rem   switch-shell.cmd install <full path to shell.exe> [/force]
rem   switch-shell.cmd restore
rem   switch-shell.cmd help
rem
rem Safety rules enforced here:
rem   * install refuses to run outside a VMware VM unless /force is given
rem   * the previous shell value is always backed up before install
rem   * restore rolls back to the backed-up value (explorer.exe if none)
rem
rem Environment overrides for testing only:
rem   SWITCH_SHELL_TESTKEY=HKCU\...  redirects all writes to a scratch key
rem   SWITCH_SHELL_DRYRUN=1          prints what would happen, writes nothing
rem
rem Registry writes are performed by shell-registry.ps1 (same directory);
rem values are passed via environment variables so paths with spaces and
rem quotes are never mangled by command-line parsing.
rem ===========================================================================

set "DRYRUN="
if defined SWITCH_SHELL_DRYRUN (
    set "DRYRUN=1"
    echo [DRY RUN] no registry changes will be made
)

set "KEY=HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon"
if defined SWITCH_SHELL_TESTKEY (
    set "KEY=!SWITCH_SHELL_TESTKEY!"
    echo [TEST MODE] using registry key: !KEY!
)

set "BACKUP_DIR=%USERPROFILE%\.plasma-windows"
if defined SWITCH_SHELL_TESTKEY set "BACKUP_DIR=%TEMP%\plasma-windows-test"
set "BACKUP=%BACKUP_DIR%\shell-backup.txt"

rem Convert HKCU\... to the PowerShell provider path HKCU:\...
set "SHELL_KEY_PS="
for /f "tokens=1,* delims=\" %%A in ("%KEY%") do set "SHELL_KEY_PS=%%A:\%%B"

goto :main

rem ---------------------------------------------------------------------------
:usage
echo Usage: switch-shell.cmd ^<command^> [options]
echo.
echo Commands:
echo   status                     Show the current shell configuration
echo   install ^<path\shell.exe^>  Configure the current user's shell
echo   restore                    Roll back to the previous shell value
echo   help                       Show this help
echo.
echo Options:
echo   /force                     Skip the VMware safety check
echo.
echo Notes:
echo   * Only the current user (HKCU) is changed, no system binaries.
echo   * The change takes effect after logoff/logon or a VM restart.
echo   * Recovery: Ctrl+Shift+Esc, File^>Run new task, cmd.exe, then run
echo     "switch-shell.cmd restore".
exit /b 0

:error_usage
echo ERROR: %*
echo.
exit /b 1

rem ---------------------------------------------------------------------------
:status
echo.
echo Registry key: %KEY%
echo.
echo Current "Shell" value:
reg query "%KEY%" /v Shell 2>nul
if errorlevel 1 echo     (not set - the default explorer.exe is used)
echo.
if exist "!BACKUP!" (
    echo Backup file: !BACKUP!
    echo Previous value:
    set /p PREV=<"!BACKUP!"
    echo     !PREV!
) else (
    echo Backup file: !BACKUP!
    echo     ^(no backup yet^)
)
exit /b 0

rem ---------------------------------------------------------------------------
:install
if "%~2"=="" goto :error_usage
set "NEW="
for %%F in ("%~2") do set "NEW=%%~fF"
if not exist "%NEW%" (
    echo ERROR: file not found: !NEW!
    exit /b 1
)
if exist "%NEW%\" (
    echo ERROR: not a file: !NEW!
    exit /b 1
)
set "FORCE="
if /i "%~3"=="/force" set "FORCE=1"

rem Safety check: refuse to install on a non-VMware machine unless /force.
if not defined FORCE (
    powershell -NoProfile -Command "if((Get-WmiObject Win32_ComputerSystem).Manufacturer -match 'VMware'){exit 0}else{exit 1}"
    if errorlevel 1 (
        echo.
        echo ERROR: this machine does not look like a VMware virtual machine.
        echo   The experimental shell must only be installed inside the test VM.
        echo   If you are certain this is the test VM, re-run with:  install !NEW! /force
        exit /b 2
    )
)

rem Quote the path if it contains spaces (Winlogon parses it as a command line).
set "Q="
if not "!NEW!"=="!NEW: =!" set "Q=""
set "QUOTED=!Q!!NEW!!Q!"

set "SHELL_VALUE=!QUOTED!"
set "SHELL_BACKUP=!BACKUP!"
if defined DRYRUN (
    echo [DRY RUN] would back up the current value to !BACKUP!
    echo [DRY RUN] would set !KEY! Shell = !QUOTED!
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0shell-registry.ps1" -Action install
    if errorlevel 1 (
        echo ERROR: failed to write the registry value.
        exit /b 3
    )
)
echo Installed shell: !NEW!
echo.
echo Next step: log off and back on (or restart the VM).
echo To undo: run "switch-shell.cmd restore" then log off/on again.
exit /b 0

rem ---------------------------------------------------------------------------
:restore
rem The backup is restored verbatim (explicit rollback). If no backup
rem exists, the Windows default explorer.exe is restored.
set "PREV=explorer.exe"
if exist "!BACKUP!" set /p PREV=<"!BACKUP!"

set "SHELL_BACKUP=!BACKUP!"
if defined DRYRUN (
    echo [DRY RUN] would set !KEY! Shell = !PREV!
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0shell-registry.ps1" -Action restore
    if errorlevel 1 (
        echo ERROR: failed to write the registry value.
        exit /b 3
    )
)
echo.
echo Restored. Log off and back on (or restart the VM) to finish.
exit /b 0

rem ---------------------------------------------------------------------------
:main
if "%~1"=="" goto :usage
if /i "%~1"=="help" goto :usage
if /i "%~1"=="status" goto :status
if /i "%~1"=="install" goto :install
if /i "%~1"=="restore" goto :restore
echo Unknown command: %~1
echo.
goto :usage
