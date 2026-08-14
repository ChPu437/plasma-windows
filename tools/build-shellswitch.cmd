@echo off
setlocal EnableExtensions
rem ===========================================================================
rem build-shellswitch.cmd - build the tray-resident shell switcher
rem
rem Produces:
rem   <repo>\tmp\shellswitch_out\shellswitch.exe
rem   (and publishes a copy to %CRAFT%\bin\shellswitch.exe when CRAFT is set)
rem
rem Pure Win32 (user32/shell32), zero Qt/KDE dependencies - it must keep
rem running no matter which shell is active.
rem
rem Usage (from a vcvars64 x64 prompt):
rem   tools\build-shellswitch.cmd
rem ===========================================================================

set "OUT=%~dp0..\tmp\shellswitch_out"
set "SRC=%~dp0..\src\shellswitch"

if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /EHsc /MD /utf-8 /O2 /DNDEBUG /DUNICODE /D_UNICODE /std:c17 ^
    "%SRC%\shellswitch.c" ^
    /Fo"%OUT%\\" /link user32.lib shell32.lib advapi32.lib /OUT:"%OUT%\shellswitch.exe"
if errorlevel 1 (
    echo FAILED
    exit /b 1
)
echo built: %OUT%\shellswitch.exe

if defined CRAFT (
    copy /y "%OUT%\shellswitch.exe" "%CRAFT%\bin\shellswitch.exe" >nul
    echo published: %CRAFT%\bin\shellswitch.exe
)
exit /b 0
