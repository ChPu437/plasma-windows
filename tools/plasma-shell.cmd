@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 936 >nul
title Plasma Shell 会话管理 (VM)

rem ===========================================================================
rem plasma-shell.cmd - 交互式 Plasma 会话管理（在测试 VM 中运行）
rem
rem 用法: 双击或在 cmd 中运行本脚本，按菜单操作。
rem
rem   1. 临时启动 Plasma 会话（不修改任何系统配置）
rem   2. 设置为默认 Shell（备份当前值，下次登录生效，可随时回滚）
rem   3. 恢复 Explorer 为默认 Shell（回滚）
rem   4. 查看当前状态
rem   0. 退出
rem
rem 只修改当前用户的 HKCU\...\Winlogon\Shell 值，不碰系统二进制和 Winlogon。
rem 环境/镜像/菜单/dbus/ksycoca 逻辑在 plasma-common.cmd（同目录）。
rem ===========================================================================

rem -------- 定位 CraftRoot（脚本所在目录）--------
set "CRAFT_ROOT=%~dp0"
if "%CRAFT_ROOT:~-1%"=="\" set "CRAFT_ROOT=%CRAFT_ROOT:~0,-1%"
set "CRAFT_BIN=%CRAFT_ROOT%\bin"
set "SHELL_KEY=HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon"
set "BACKUP_DIR=%USERPROFILE%\.plasma-windows"
set "SHELL_BACKUP=%BACKUP_DIR%\shell-backup.txt"
set "SESSION_LAUNCHER=%CRAFT_ROOT%\session-shell.cmd"

rem -------- 检查 plasmashell --------
if not exist "%CRAFT_BIN%\plasmashell.exe" (
    echo.
    echo [错误] 未找到 %CRAFT_BIN%\plasmashell.exe
    echo        请确认本脚本位于 CraftRoot 目录中（与 bin 目录平级）。
    pause
    exit /b 1
)

goto :menu

rem ---------------------------------------------------------------------------
rem :env_setup - 环境变量 + 镜像 KDE 数据到 %LOCALAPPDATA%
rem ---------------------------------------------------------------------------
:env_setup
call "%~dp0plasma-common.cmd" :pc_setup_env
echo [准备] 镜像 KDE 包数据到 %LOCALAPPDATA% ...
call "%~dp0plasma-common.cmd" :pc_mirror_data
call "%~dp0plasma-common.cmd" :pc_write_menu
echo [准备] 重建 ksycoca 服务数据库 ...
call "%~dp0plasma-common.cmd" :pc_rebuild_ksycoca
exit /b 0

rem ---------------------------------------------------------------------------
rem :start_session - 启动 dbus + 服务 + plasmashell（前台，临时会话）
rem ---------------------------------------------------------------------------
:start_session
echo.
echo [会话] 启动会话总线 ...
call "%~dp0plasma-common.cmd" :pc_start_bus
echo [会话] 启动 kactivitymanagerd / kded6 ...
call "%~dp0plasma-common.cmd" :pc_start_services
echo [会话] 启动 plasmashell（关闭它即结束临时会话）...
"%CRAFT_BIN%\plasmashell.exe"
echo [会话] plasmashell 已退出，代码 %errorlevel%
echo.
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :install - 备份当前 shell 并设为默认（下次登录生效）
rem ---------------------------------------------------------------------------
:install
echo.
echo [安装] 将当前用户默认 Shell 切换为 Plasma ...
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"

set "CURRENT_SHELL="
for /f "usebackq skip=2 tokens=1,* delims= " %%A in (`reg query "%SHELL_KEY%" /v Shell 2^>nul`) do (
    if "%%A"=="Shell" set "CURRENT_SHELL=%%B"
)
if defined CURRENT_SHELL (
    > "%SHELL_BACKUP%" echo %CURRENT_SHELL%
    echo        已备份当前 Shell: %CURRENT_SHELL%
) else (
    > "%SHELL_BACKUP%" echo explorer.exe
    echo        未找到原 Shell 值，回滚时将恢复 explorer.exe
)

reg add "%SHELL_KEY%" /v Shell /t REG_SZ /d "cmd.exe /c \"%SESSION_LAUNCHER%\"" /f >nul
if errorlevel 1 (
    echo [错误] 写注册表失败。
    pause
    exit /b 1
)
echo        已设置: cmd.exe /c "%SESSION_LAUNCHER%"
echo        下次注销/重启登录后生效。
echo.
echo        恢复方法: 重新运行本脚本，选择 [3]。
echo.
set /p GO=是否现在临时启动 Plasma 测试？（Y/N）:
if /i "%GO%"=="Y" (
    call :env_setup
    call :start_session
)
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :restore - 回滚到备份的 shell 值
rem ---------------------------------------------------------------------------
:restore
echo.
echo [恢复] 还原默认 Shell ...
set "OLD_SHELL=explorer.exe"
if exist "%SHELL_BACKUP%" (
    set /p OLD_SHELL=<"%SHELL_BACKUP%"
    if not defined OLD_SHELL set "OLD_SHELL=explorer.exe"
)
reg add "%SHELL_KEY%" /v Shell /t REG_SZ /d "%OLD_SHELL%" /f >nul
if errorlevel 1 (
    echo [错误] 写注册表失败。
    pause
    exit /b 1
)
echo        已恢复: %OLD_SHELL%
echo        下次注销/重启登录后生效。
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :status - 查看当前 shell 配置和备份
rem ---------------------------------------------------------------------------
:status
echo.
echo [状态] 注册表键: %SHELL_KEY%
echo.
echo        当前 Shell 值:
reg query "%SHELL_KEY%" /v Shell 2>nul
if errorlevel 1 echo            （未设置 - 使用系统默认 explorer.exe）
echo.
if exist "%SHELL_BACKUP%" (
    echo        备份文件: %SHELL_BACKUP%
    set /p PREV=<"%SHELL_BACKUP%"
    echo        备份值  : !PREV!
) else (
    echo        备份文件: %SHELL_BACKUP%  （尚无备份）
)
echo.
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem :menu - 主菜单
rem ---------------------------------------------------------------------------
:menu
cls
echo.
echo  ============================================
echo      Plasma Shell 会话管理（测试 VM）
echo  ============================================
echo.
echo    CraftRoot: %CRAFT_ROOT%
echo.
echo    [1] 临时启动 Plasma 会话（不修改配置）
echo    [2] 设置为默认 Shell（备份后，下次登录生效）
echo    [3] 恢复 Explorer 为默认 Shell（回滚）
echo    [4] 查看当前状态
echo    [0] 退出
echo.
set /p CHOICE=请选择:

if "%CHOICE%"=="1" (
    call :env_setup
    call :start_session
    goto :menu
)
if "%CHOICE%"=="2" goto :install
if "%CHOICE%"=="3" goto :restore
if "%CHOICE%"=="4" goto :status
if "%CHOICE%"=="0" (
    endlocal
    exit /b 0
)
echo.
echo  无效选择，请重新输入。
timeout /t 2 /nobreak >nul
goto :menu
