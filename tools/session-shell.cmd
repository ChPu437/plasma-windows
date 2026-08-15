@echo off
setlocal EnableExtensions
rem ===========================================================================
rem session-shell.cmd - session host used as the Windows logon shell
rem
rem This is what the registry value
rem   HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell
rem points at (via cmd.exe /c) after `deploy-vm.cmd install`.
rem
rem It sets up the environment (plasma-common.cmd), starts the session bus
rem and services, then runs plasmashell in the foreground. Closing
rem plasmashell (or this window) ends the session.
rem
rem Location-independent: package root = this script's folder.
rem ===========================================================================

call "%~dp0plasma-common.cmd" :pc_resolve_root

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

call "%~dp0plasma-common.cmd" :pc_setup_env
call "%~dp0plasma-common.cmd" :pc_mirror_data

call "%~dp0plasma-common.cmd" :pc_start_bus
call "%~dp0plasma-common.cmd" :pc_start_services

rem Tray-resident shell switcher: also acts as the kded6/trayhost watchdog
rem (restarts them if they die while a plasma session is up). Single
rem instance - the HKCU Run key covers the explorer-shell case, this
rem covers the plasma-shell case.
if exist "%CRAFT_BIN%\shellswitch.exe" (
    start "plasma-shellswitch" /D "%CRAFT_BIN%" "%CRAFT_BIN%\shellswitch.exe"
)

rem Run the shell in the foreground; keep this script alive as session host.
"%CRAFT_BIN%\plasmashell.exe"
exit /b %errorlevel%
