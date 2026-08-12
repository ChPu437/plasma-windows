@echo off
setlocal EnableExtensions
rem ===========================================================================
rem vm-diagnostics.cmd - collect plasmashell diagnostics inside the VM
rem
rem Run this in the VM (double-click or from cmd). It copies:
rem   - %TEMP%\plasmashell-debug.log
rem   - the latest plasmashell crash/hang dump (if procdump already captured one)
rem into the VMware shared folder (host: D:\documents\shared).
rem
rem If plasmashell is hung right now, this script can also capture a dump
rem of the hung process before copying everything out.
rem ===========================================================================

set "SHARE=\\vmware-host\Shared Folders\shared"
if not exist "%SHARE%" (
    echo WARNING: shared folder not visible. Enable VMware shared folders
    echo (VM settings - Options - Shared Folders) and install VMware Tools.
    set "SHARE=C:\vm-shared"
    if not exist "%SHARE%" mkdir "%SHARE%"
)

echo === 1. Capturing a dump of the hung plasmashell (if any) ===
"%SHARE%\procdump64.exe" -accepteula -ma -o plasmashell "C:\vm-shared\plasmashell-hang.dmp" >nul 2>&1
if exist "C:\vm-shared\plasmashell-hang.dmp" (
    echo Dump captured.
) else (
    echo No running plasmashell to dump (or it is healthy).
)

echo === 2. Copying plasmashell debug log ===
if exist "%TEMP%\plasmashell-debug.log" (
    copy /y "%TEMP%\plasmashell-debug.log" "%SHARE%\" >nul
    echo Log copied: %TEMP%\plasmashell-debug.log
) else (
    echo No %TEMP%\plasmashell-debug.log found.
)

echo === 3. Copying session logs ===
if exist "%LOCALAPPDATA%\plasma-win-session\kded6.log" (
    copy /y "%LOCALAPPDATA%\plasma-win-session\kded6.log" "%SHARE%\" >nul
)
if exist "%USERPROFILE%\.plasma-windows\shell-backup.txt" (
    copy /y "%USERPROFILE%\.plasma-windows\shell-backup.txt" "%SHARE%\" >nul
)

echo.
echo Diagnostics copied to %SHARE%. Tell the dev to check D:\documents\shared.
pause
endlocal
