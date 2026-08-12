@echo off
setlocal EnableExtensions
rem ===========================================================================
rem make-vm-package.cmd - build the VM deployment package (run on the dev box)
rem
rem Creates a 7z archive with the CraftRoot runtime (bin, lib, plugins, qml,
rem include, share, etc. - excluding build trees, downloads and dev tools)
rem plus the session bootstrap scripts, ready to copy into the test VM.
rem
rem Usage: make-vm-package.cmd [outdir]
rem   default outdir: D:\Projects\plasma-windows\vm-package
rem ===========================================================================

set "CRAFT_ROOT=D:\Projects\CraftRoot"
set "OUTDIR=%~1"
if "%OUTDIR%"=="" set "OUTDIR=D:\Projects\plasma-windows\vm-package"
set "SEVENZIP=%CRAFT_ROOT%\dev-utils\bin\7za.exe"
set "PKG=%OUTDIR%\plasma-vm.zip"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo Packing CraftRoot runtime (this takes a while)...
"%SEVENZIP%" a -tzip -mx=3 -xr!build -xr!download -xr!dev-utils -xr!logs -xr!doc -xr!html -xr!man -xr!tests -xr!msys -xr!include -xr!certs -xr!var -xr!translations -xr!share\locale -xr!share\doc "%PKG%" "%CRAFT_ROOT%\*" >nul
if errorlevel 1 (
    echo FAILED to create package.
    exit /b 1
)

echo Copying session bootstrap into the package staging...
copy /y "%~dp0start-plasma-session.cmd" "%OUTDIR%\" >nul
copy /y "%~dp0deploy-vm.cmd" "%OUTDIR%\" >nul
copy /y "%~dp0dbus\session-plasma.conf" "%OUTDIR%\dbus-session-plasma.conf" >nul 2>&1

echo.
echo ===========================================================================
echo VM package ready:
echo   %PKG%
echo   (%OUTDIR%\deploy-vm.cmd  - run this inside the VM after extracting)
echo ===========================================================================
echo.
echo Next steps:
echo   1. Copy %OUTDIR% into the VM (shared folder or drag-and-drop).
echo   2. In the VM, extract plasma-vm.zip to D:\Projects\CraftRoot
echo      (Windows built-in: right-click Extract All, or
echo       powershell Expand-Archive plasma-vm.zip -DestinationPath D:\Projects )
echo   3. Run deploy-vm.cmd (see that file for details).
endlocal
