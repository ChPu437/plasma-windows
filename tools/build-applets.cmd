@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ===========================================================================
rem build-applets.cmd - build the native Plasma Windows applets
rem (volumewin, imewin) without a full craft blueprint
rem
rem Runs moc + cl manually against the craft-installed headers/libs.
rem Produces:
rem   CraftRoot\plugins\plasma\applets\org.kde.plasma.<name>.dll
rem (package data is mirrored by the session scripts / update-vm-shared.ps1)
rem
rem Usage (from a vcvars64 x64 prompt):
rem   tools\build-applets.cmd
rem ===========================================================================

set "CRAFT=D:\Projects\CraftRoot"
set "OUT=D:\_applet_out"
set "VSSRC=D:\Projects\plasma-windows\src\applets"

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%CRAFT%\plugins\plasma\applets" mkdir "%CRAFT%\plugins\plasma\applets"

set "INC=-I%CRAFT%\include -I%CRAFT%\include\QtCore -I%CRAFT%\include\QtGui -I%CRAFT%\include\QtQml -I%CRAFT%\include\QtQuick -I%CRAFT%\include\QtNetwork -I%CRAFT%\include\QtDBus -I%CRAFT%\include\KF6 -I%CRAFT%\include\Plasma -I%CRAFT%\include\PlasmaQuick -I%CRAFT%\mkspecs\win32-msvc"
for /d %%D in ("%CRAFT%\include\KF6\*") do set "INC=!INC! -I%%D"
set "DEFS=-DUNICODE -D_UNICODE -DWIN32 -DWIN64 -DNOMINMAX -DQT_NO_DEBUG -DQT_CORE_LIB -DQT_GUI_LIB -DQT_QML_LIB -DQT_QUICK_LIB -DQT_NETWORK_LIB"
set "LIBS=/LIBPATH:%CRAFT%\lib Qt6Core.lib Qt6Gui.lib Qt6Qml.lib Qt6Quick.lib Plasma.lib PlasmaQuick.lib KF6CoreAddons.lib KF6I18n.lib ole32.lib uuid.lib user32.lib"

for %%A in (volumewin imewin) do (
    echo === %%A ===
    pushd "%VSSRC%\%%A"
    if exist volumecontroller.h (
        "%CRAFT%\bin\moc.exe" %INC% volumecontroller.h -o "%OUT%\moc_volumecontroller.cpp"
        set "CTRL_SRC=volumecontroller.cpp"
        set "CTRL_MOC=%OUT%\moc_volumecontroller.cpp"
    ) else (
        set "CTRL_SRC=imecontroller.cpp"
        set "CTRL_MOC="
    )
    if exist imecontroller.h (
        "%CRAFT%\bin\moc.exe" %INC% imecontroller.h -o "%OUT%\moc_imecontroller.cpp"
        set "CTRL_MOC=!CTRL_MOC! %OUT%\moc_imecontroller.cpp"
    )
    "%CRAFT%\bin\moc.exe" %INC% %%A.cpp -o "%OUT%\%%A.moc"
    cl /nologo /LD /EHsc /MD /utf-8 /O2 /Ob1 /DNDEBUG /Zc:__cplusplus /permissive- /std:c++17 %DEFS% %INC% -I"%OUT%" ^
        %%A.cpp !CTRL_SRC! ^
        /Fo"%OUT%\\" /link %LIBS% /OUT:"%OUT%\org.kde.plasma.%%A.dll"
    if errorlevel 1 (
        echo FAILED: %%A
        popd
        exit /b 1
    )
    copy /y "%OUT%\org.kde.plasma.%%A.dll" "%CRAFT%\plugins\plasma\applets\" >nul
    echo installed: plugins\plasma\applets\org.kde.plasma.%%A.dll
    popd
)
echo All applets built.
exit /b 0
