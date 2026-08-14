@echo off
setlocal EnableExtensions
rem ===========================================================================
rem build-titlebar.cmd - build the L4 caption-removal engine
rem
rem Produces:
rem   <repo>\tmp\titlebar_out\titlebar.exe
rem
rem Pure Win32 (user32/dwmapi), no Qt/KDE/Plasma dependencies.
rem
rem Usage (from a vcvars64 x64 prompt):
rem   tools\build-titlebar.cmd
rem ===========================================================================

set "OUT=%~dp0..\tmp\titlebar_out"
set "SRC=%~dp0..\src\titlebar"

if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /EHsc /MD /utf-8 /O2 /DNDEBUG /DUNICODE /D_UNICODE /std:c17 ^
    "%SRC%\titlebar.c" ^
    /Fo"%OUT%\\" /link user32.lib advapi32.lib dwmapi.lib /OUT:"%OUT%\titlebar.exe"
if errorlevel 1 (
    echo FAILED
    exit /b 1
)
echo built: %OUT%\titlebar.exe

if defined CRAFT (
    copy /y "%OUT%\titlebar.exe" "%CRAFT%\bin\titlebar.exe" >nul
    echo published: %CRAFT%\bin\titlebar.exe
)
exit /b 0
