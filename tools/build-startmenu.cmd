@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ===========================================================================
rem build-startmenu.cmd - build the org.kde.plasma.windowsmenu QML module
rem (StartMenuModel used by the kickoff windows menu view)
rem
rem Produces:
rem   CraftRoot\qml\org\kde\plasma\windowsmenu\startmenuplugin.dll + qmldir
rem
rem Usage (from a vcvars64 x64 prompt):
rem   tools\build-startmenu.cmd
rem ===========================================================================

set "CRAFT=D:\Projects\CraftRoot"
set "OUT=D:\_startmenu_out"
set "SRC=D:\Projects\plasma-windows\src\startmenu"

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%CRAFT%\qml\org\kde\plasma\windowsmenu" mkdir "%CRAFT%\qml\org\kde\plasma\windowsmenu"

set "INC=-I%CRAFT%\include -I%CRAFT%\include\QtCore -I%CRAFT%\include\QtGui -I%CRAFT%\include\QtQml -I%CRAFT%\include\QtQuick -I%CRAFT%\include\KF6 -I%CRAFT%\include\kworkspace6 -I%CRAFT%\mkspecs\win32-msvc -I%SRC%"
for /d %%D in ("%CRAFT%\include\KF6\*") do set "INC=!INC! -I%%D"
set "DEFS=-DUNICODE -D_UNICODE -DWIN32 -DWIN64 -DNOMINMAX -DQT_NO_DEBUG -DQT_CORE_LIB -DQT_GUI_LIB -DQT_QML_LIB -DQT_NETWORK_LIB"
set "LIBS=/LIBPATH:%CRAFT%\lib Qt6Core.lib Qt6Gui.lib Qt6Qml.lib Qt6Quick.lib kworkspace6.lib ole32.lib user32.lib shell32.lib gdi32.lib"

echo === moc ===
"%CRAFT%\bin\moc.exe" %INC% "%SRC%\startmenumodel.h" -o "%OUT%\moc_startmenumodel.cpp"
"%CRAFT%\bin\moc.exe" %INC% "%SRC%\poweractions.h" -o "%OUT%\moc_poweractions.cpp"
"%CRAFT%\bin\moc.exe" %INC% "%SRC%\plugin.cpp" -o "%OUT%\plugin.moc"

echo === cl ===
cl /nologo /LD /EHsc /MD /utf-8 /O2 /Ob1 /DNDEBUG /Zc:__cplusplus /permissive- /std:c++17 %DEFS% %INC% -I"%OUT%" ^
    "%SRC%\startmenumodel.cpp" "%SRC%\startmenuimageprovider.cpp" "%SRC%\poweractions.cpp" "%SRC%\plugin.cpp" ^
    "%OUT%\moc_startmenumodel.cpp" "%OUT%\moc_poweractions.cpp" ^
    /Fo"%OUT%\\" /link %LIBS% /OUT:"%OUT%\startmenuplugin.dll"
if errorlevel 1 (
    echo FAILED
    exit /b 1
)

copy /y "%OUT%\startmenuplugin.dll" "%CRAFT%\qml\org\kde\plasma\windowsmenu\" >nul
copy /y "%SRC%\qmldir" "%CRAFT%\qml\org\kde\plasma\windowsmenu\" >nul
echo installed: qml\org\kde\plasma\windowsmenu\
exit /b 0

