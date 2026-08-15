@echo off
setlocal EnableExtensions
rem ===========================================================================
rem update-from-shared.cmd - run inside the VM: pull the latest build from
rem the VMware shared folder, mirror it over the local CraftRoot and restart
rem the Plasma shell.
rem
rem Why this script exists: the VM runs Plasma as the default shell, so
rem plasmashell / kactivitymanagerd / kded6 / dbus-daemon / krunner etc.
rem keep the old DLLs locked - a plain copy fails. This script stops those
rem services first (releasing the locks), mirrors the new files from the
rem shared folder, then restarts the Plasma session.
rem
rem Usage (in the VM, cmd window or double-click):
rem   \\vmware-host\Shared Folders\shared\plasma-vm\update-from-shared.cmd
rem
rem After the copy the script also lands in CraftRoot, so next time you can
rem run
rem   D:\Projects\CraftRoot\update-from-shared.cmd
rem ===========================================================================

set "SHARE=\\vmware-host\Shared Folders\shared\plasma-vm"
set "CRAFT=D:\Projects\CraftRoot"

rem ---------- 0. shared folder sanity check ----------
if not exist "%SHARE%\bin\plasmashell.exe" (
    echo.
    echo [ERROR] Shared folder not available: %SHARE%
    echo Make sure VMware Shared Folders is enabled (VM Settings - Options -
    echo Shared Folders), the share is named "shared" and contains the
    echo plasma-vm subdirectory.
    echo.
    pause
    exit /b 1
)

rem ---------- 1. stop Plasma services (release file locks) ----------
echo.
echo === [1/3] Stopping Plasma services (releasing file locks) ===
taskkill /f /im plasmashell.exe       2>nul
taskkill /f /im kactivitymanagerd.exe 2>nul
taskkill /f /im kded6.exe             2>nul
taskkill /f /im trayhost.exe           2>nul
taskkill /f /im dbus-daemon.exe       2>nul
taskkill /f /im krunner.exe           2>nul
taskkill /f /im kglobalacceld.exe     2>nul
taskkill /f /im klipper.exe           2>nul
taskkill /f /im kbuildsycoca6.exe     2>nul
timeout /t 3 /nobreak >nul
echo Stopped (the desktop disappears temporarily - expected).

rem ---------- 2. mirror copy (retry once on locked files) ----------
echo.
echo === [2/3] Copying %SHARE% -^> %CRAFT% ===
:retry_copy
robocopy "%SHARE%" "%CRAFT%" /MIR /R:2 /W:2 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo.
    echo [WARN] Copy hit locked files - stopping Plasma processes again and retrying...
    taskkill /f /im plasmashell.exe       2>nul
    taskkill /f /im kactivitymanagerd.exe 2>nul
    taskkill /f /im kded6.exe             2>nul
    taskkill /f /im trayhost.exe           2>nul
    taskkill /f /im dbus-daemon.exe       2>nul
    taskkill /f /im krunner.exe           2>nul
    taskkill /f /im kglobalacceld.exe     2>nul
    taskkill /f /im klipper.exe           2>nul
    taskkill /f /im kbuildsycoca6.exe     2>nul
    timeout /t 3 /nobreak >nul
    robocopy "%SHARE%" "%CRAFT%" /MIR /R:2 /W:2 /NFL /NDL /NJH /NJS /NP
)
if errorlevel 8 (
    echo.
    echo [ERROR] Copy still failed (robocopy exit code %errorlevel%).
    echo Some process still holds CraftRoot files. Check Task Manager for
    echo leftover processes (plasmashell / kded6 / krunner / dbus etc.),
    echo kill them and retry.
    pause
    exit /b 1
)
echo Copy done.

rem ---------- 3. restart the Plasma shell ----------
echo.
echo === [3/3] Restarting the Plasma shell ===
call "%CRAFT%\session-shell.cmd"
