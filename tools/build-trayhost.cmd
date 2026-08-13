@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ===========================================================================
rem build-trayhost.cmd - build the WindowsTrayHost daemon (Phase 2)
rem
rem Produces:
rem   D:\_trayhost_out\trayhost.exe
rem
rem Usage (from a vcvars64 x64 prompt):
rem   tools\build-trayhost.cmd
rem ===========================================================================

set "CRAFT=D:\Projects\CraftRoot"
set "OUT=D:\_trayhost_out"
set "SRC=D:\Projects\plasma-windows\src\trayhost"

if not exist "%OUT%" mkdir "%OUT%"

set "INC=-I%CRAFT%\include -I%CRAFT%\include\QtCore -I%CRAFT%\include\KF6 -I%CRAFT%\mkspecs\win32-msvc -I%SRC%"
for /d %%D in ("%CRAFT%\include\KF6\*") do set "INC=!INC! -I%%D"
set "DEFS=-DUNICODE -D_UNICODE -DWIN32 -DWIN64 -DNOMINMAX -DQT_NO_DEBUG -DQT_CORE_LIB -DQT_GUI_LIB"
set "LIBS=/LIBPATH:%CRAFT%\lib Qt6Core.lib user32.lib shell32.lib gdi32.lib"

echo === moc ===
"%CRAFT%\bin\moc.exe" %INC% "%SRC%\windowstrayhost.h" -o "%OUT%\moc_windowstrayhost.cpp"
if errorlevel 1 exit /b 1

echo === cl ===
cl /nologo /EHsc /MD /utf-8 /O2 /Ob1 /DNDEBUG /Zc:__cplusplus /permissive- /std:c++17 %DEFS% %INC% -I"%OUT%" ^
    "%SRC%\trayhost_main.cpp" "%SRC%\windowstrayhost.cpp" "%OUT%\moc_windowstrayhost.cpp" ^
    /Fo"%OUT%\\" /link %LIBS% /OUT:"%OUT%\trayhost.exe"
if errorlevel 1 (
    echo FAILED
    exit /b 1
)
echo built: %OUT%\trayhost.exe
exit /b 0

