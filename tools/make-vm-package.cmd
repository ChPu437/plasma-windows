@echo off
setlocal EnableExtensions
rem ===========================================================================
rem make-vm-package.cmd - build the VM deployment package (run on the dev box)
rem
rem Creates a zip with the CraftRoot runtime (bin, lib, plugins, qml,
rem include, share, etc. - excluding build trees, downloads and dev tools)
rem plus deploy-vm.cmd and the dbus config in the archive root, so the VM
rem only needs one file: extract plasma-vm.zip anywhere, then run the
rem deploy-vm.cmd that lands next to bin\.
rem
rem Location-independent: deploy-vm.cmd resolves the package root from its
rem own location, so no drive/path is hard-coded.
rem
rem Usage: make-vm-package.cmd [outdir]
rem   default outdir: D:\Projects\plasma-windows\vm-package
rem ===========================================================================

set "CRAFT_ROOT=D:\Projects\CraftRoot"
set "OUTDIR=%~1"
if "%OUTDIR%"=="" set "OUTDIR=D:\Projects\plasma-windows\vm-package"
set "SEVENZIP=%CRAFT_ROOT%\dev-utils\bin\7za.exe"
set "PKG=%OUTDIR%\plasma-vm.zip"
set "STAGE=%OUTDIR%\stage"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

rem Stage the runtime contents (CraftRoot\* -> stage\ so scripts can be added).
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
echo Staging runtime files (copy, this takes a while)...
robocopy "%CRAFT_ROOT%" "%STAGE%" /E /XD build download dev-utils logs doc html man tests msys include certs var translations /XF *.pdb /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo FAILED to stage files.
    exit /b 1
)

rem Drop the session scripts into the archive root next to bin\.
copy /y "%~dp0deploy-vm.cmd" "%STAGE%\" >nul
copy /y "%~dp0dbus\session-plasma.conf" "%STAGE%\dbus-session-plasma.conf" >nul 2>&1

echo Packing zip (this takes a while)...
"%SEVENZIP%" a -tzip -mx=3 "%PKG%" "%STAGE%\*" >nul
if errorlevel 1 (
    echo FAILED to create package.
    exit /b 1
)
rmdir /s /q "%STAGE%"

echo.
echo ===========================================================================
echo VM package ready:
echo   %PKG%
echo ===========================================================================
echo.
echo Next steps (VM):
echo   1. Copy plasma-vm.zip into the VM (shared folder or drag-and-drop).
echo   2. Extract it anywhere, e.g.:
echo        powershell Expand-Archive plasma-vm.zip -DestinationPath C:\plasma
echo   3. Run C:\plasma\deploy-vm.cmd
echo      (the zip contains deploy-vm.cmd + dbus-session-plasma.conf next
echo       to bin\; no other file is needed)
endlocal
