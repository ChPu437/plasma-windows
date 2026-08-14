@echo off
setlocal EnableExtensions
rem ===========================================================================
rem update-from-shared.cmd - 在 VM 中运行：从 VMware 共享文件夹更新本机
rem CraftRoot 并重启 Plasma shell
rem
rem 为什么需要本脚本：VM 现在以 Plasma 作为默认 shell 运行，plasmashell /
rem kactivitymanagerd / kded6 / dbus-daemon / krunner 等进程持续占用旧版
rem DLL，直接复制会失败。本脚本先彻底停止这些服务（解除占用），再镜像
rem 复制共享文件夹的新版本，最后重启 Plasma 会话。
rem
rem 用法（在 VM 中，cmd 窗口或双击）:
rem   \\vmware-host\Shared Folders\shared\plasma-vm\update-from-shared.cmd
rem
rem 复制完成后脚本也会把自己放进 CraftRoot，下次可直接运行
rem   D:\Projects\CraftRoot\update-from-shared.cmd
rem ===========================================================================

set "SHARE=\\vmware-host\Shared Folders\shared\plasma-vm"
set "CRAFT=D:\Projects\CraftRoot"

rem ---------- 0. 检查共享可用 ----------
if not exist "%SHARE%\bin\plasmashell.exe" (
    echo.
    echo [错误] 共享文件夹不可用: %SHARE%
    echo 请确认 VMware 共享文件夹已启用（VM 设置 - Options - Shared Folders）
    echo 且共享目录名为 shared，子目录为 plasma-vm。
    echo.
    pause
    exit /b 1
)

rem ---------- 1. 停止 Plasma 服务（解除文件占用） ----------
echo.
echo === [1/3] 停止 Plasma 服务（解除文件占用）===
taskkill /f /im plasmashell.exe       2>nul
taskkill /f /im kactivitymanagerd.exe 2>nul
taskkill /f /im kded6.exe             2>nul
taskkill /f /im trayhost.exe           2>nul
taskkill /f /im dbus-daemon.exe       2>nul
taskkill /f /im krunner.exe           2>nul
taskkill /f /im kglobalacceld.exe     2>nul
taskkill /f /im klipper.exe           2>nul
taskkill /f /im kbuildsycoca6.exe     2>nul
timeout /t 3 /nobreak >nul
echo 已停止（桌面会暂时消失，属正常现象）。

rem ---------- 2. 镜像复制（占用失败时重试一次） ----------
echo.
echo === [2/3] 复制 %SHARE% -^> %CRAFT% ===
:retry_copy
robocopy "%SHARE%" "%CRAFT%" /MIR /R:2 /W:2 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo.
    echo [警告] 复制遇到被占用的文件，再次停止 Plasma 进程后重试...
    taskkill /f /im plasmashell.exe       2>nul
    taskkill /f /im kactivitymanagerd.exe 2>nul
    taskkill /f /im kded6.exe             2>nul
taskkill /f /im trayhost.exe           2>nul
    taskkill /f /im dbus-daemon.exe       2>nul
    taskkill /f /im krunner.exe           2>nul
    taskkill /f /im kglobalacceld.exe     2>nul
    taskkill /f /im klipper.exe           2>nul
    taskkill /f /im kbuildsycoca6.exe     2>nul
    timeout /t 3 /nobreak >nul
    robocopy "%SHARE%" "%CRAFT%" /MIR /R:2 /W:2 /NFL /NDL /NJH /NJS /NP
)
if errorlevel 8 (
    echo.
    echo [错误] 复制仍失败（robocopy 退出码 %errorlevel%）。
    echo 仍有进程占用 CraftRoot 文件。请用任务管理器检查残留进程
    echo （plasmashell / kded6 / krunner / dbus 等）并结束它们后重试。
    pause
    exit /b 1
)
echo 复制完成。

rem ---------- 3. 重启 Plasma shell ----------
echo.
echo === [3/3] 重启 Plasma shell ===
call "%CRAFT%\session-shell.cmd"
