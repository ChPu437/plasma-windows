@echo off
rem ============================================================================
rem build-titlebar-qml.cmd - build the QML Plasma title bar (titlebar-qml)
rem
rem Usage:  build-titlebar-qml.cmd
rem Output: tmp\titlebar-qml_out\titlebar-qml.exe
rem ============================================================================
setlocal

call "%~dp0..\tmp\vcvars64.bat" >nul 2>&1
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set "SRC=%~dp0..\src\titlebar-qml"
set "OUT=%~dp0..\tmp\titlebar-qml_out"

cmake -S "%SRC%" -B "%OUT%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_PREFIX_PATH=D:\Projects\CraftRoot
if errorlevel 1 exit /b 1

ninja -C "%OUT%"
if errorlevel 1 exit /b 1

echo built: %OUT%\titlebar-qml.exe
exit /b 0
