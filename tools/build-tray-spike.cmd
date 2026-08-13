@echo off
setlocal EnableExtensions
rem ===========================================================================
rem build-tray-spike.cmd - build the Phase 1 tray protocol spike
rem (pure Win32, no Qt)
rem
rem Produces (next to this script):
rem   trayhost-spike.exe   - the Shell_TrayWnd receiver + protocol logger
rem   trayclient-spike.exe - a Shell_NotifyIcon test client
rem
rem Usage (from a vcvars64 x64 prompt):
rem   tools\build-tray-spike.cmd
rem ===========================================================================

set "OUT=D:\Projects\plasma-windows\src\trayhost\spike"

cl /nologo /W3 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE "%OUT%\trayhost-spike.cpp" /Fe:"%OUT%\trayhost-spike.exe" /link user32.lib shell32.lib
if errorlevel 1 exit /b 1
cl /nologo /W3 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE "%OUT%\trayclient-spike.cpp" /Fe:"%OUT%\trayclient-spike.exe" /link user32.lib shell32.lib
if errorlevel 1 exit /b 1

echo built: trayhost-spike.exe + trayclient-spike.exe
exit /b 0

