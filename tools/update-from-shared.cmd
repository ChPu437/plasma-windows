@echo off
setlocal EnableExtensions
rem ===========================================================================
rem update-from-shared.cmd - 在 VM 中运行：从 VMware 共享文件夹更新本机
rem CraftRoot 并重启 Plasma shell
rem
rem 为什么需要本脚本：VM 现在以 Plasma 作为默认 shell 运行，plasmashell /
rem kactivitymanagerd / kded6 / dbus-daemon 持续占用旧版 DLL，直接复制会
rem 失败。本脚本先彻底停止这些服务（解除占用），再镜像复制共享文件夹的
rem 新版本，最后重启 Plasma 会话。
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
taskkill /f /im plasmashell.exe      2>nul
taskkill /f /im kactivitymanagerd.exe 2>nul
taskkill /f /im kded6.exe            2>nul
taskkill /f /im dbus-daemon.exe      2>nul
timeout /t 3 /nobreak >nul
echo 已停止（桌面会暂时消失，属正常现象）。

rem ---------- 2. 镜像复制 ----------
echo.
echo === [2/3] 复制 %SHARE% -^> %CRAFT% ===
robocopy "%SHARE%" "%CRAFT%" /MIR /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo.
    echo [错误] 复制失败（robocopy 退出码 %errorlevel%）。
    echo 检查共享文件夹是否断开、CraftRoot 是否有只读文件。
    pause
    exit /b 1
)
echo 复制完成。

rem ---------- 3. 重启 Plasma shell ----------
echo.
echo === [3/3] 重启 Plasma shell ===
call "%CRAFT%\session-shell.cmd"
