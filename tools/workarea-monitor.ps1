# workarea-monitor.ps1 - correlate the panel animation timeline with
# system work-area changes while the user drags a window.
#
# Logs (append, one line per change):
#   WA <ms> <L>,<T>,<R>,<B>        - SPI_GETWORKAREA changed
#   FW <ms> <hwnd>                 - foreground window changed
#   WIN <ms> <hwnd> <l>,<t>,<r>,<b> - plasmashell window moved
#   END <ms>                       - monitor stopped
#
# Usage: powershell -File tools\workarea-monitor.ps1 [-Seconds 180]
param([int]$Seconds = 180)

$log = "C:\Users\jing\AppData\Local\Temp\opencode\wa-monitor.log"
[System.IO.File]::AppendAllText($log, "START $([Environment]::TickCount)`n")

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public struct WARECT { public int L, T, R, B; }
public class WAAPI {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool SystemParametersInfo(int uiAction, int uiParam, ref WARECT pvParam, int fWinIni);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr h, out WARECT r);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
}
"@

$plasmashellPid = (Get-Process plasmashell -ErrorAction SilentlyContinue | Select-Object -First 1).Id
if (-not $plasmashellPid) { Write-Host "plasmashell not running"; exit 1 }

function Get-PlasmaWindows {
    $script:waList = @()
    $cb = { param($h, $l)
        $pid2 = 0
        [WAAPI]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
        if ($pid2 -eq $plasmashellPid) {
            $r = New-Object WARECT
            if ([WAAPI]::GetWindowRect($h, [ref]$r)) {
                $script:waList += [pscustomobject]@{ H = $h.ToInt64(); R = $r }
            }
        }
        return $true
    }
    [WAAPI]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    $script:waList
}

function Get-RectKey($r) { "$($r.L),$($r.T),$($r.R),$($r.B)" }

$t0 = [Environment]::TickCount
$deadline = $t0 + $Seconds * 1000
$lastWa = $null
$lastFg = 0
$lastFgRect = $null
$lastWins = @{}

while ([Environment]::TickCount -lt $deadline) {
    $r = New-Object WARECT
    [WAAPI]::SystemParametersInfo(48, 0, [ref]$r, 0) | Out-Null   # SPI_GETWORKAREA
    $key = "$($r.L),$($r.T),$($r.R),$($r.B)"
    $now = [Environment]::TickCount
    if ($key -ne $lastWa) {
        [System.IO.File]::AppendAllText($log, "WA $now $key`n")
        $lastWa = $key
    }
    $fg = [WAAPI]::GetForegroundWindow().ToInt64()
    if ($fg -ne $lastFg) {
        [System.IO.File]::AppendAllText($log, "FW $now $($fg.ToString('x'))`n")
        $lastFg = $fg
    }
    if ($fg -ne 0) {
        $r3 = New-Object WARECT
        if ([WAAPI]::GetWindowRect([IntPtr]$fg, [ref]$r3)) {
            $key3 = Get-RectKey $r3
            if ($lastFgRect -ne $key3) {
                [System.IO.File]::AppendAllText($log, "FWR $now $($fg.ToString('x')) $key3`n")
                $lastFgRect = $key3
            }
        }
    }
    $wins = Get-PlasmaWindows
    foreach ($w in $wins) {
        $k = $w.H
        $r2 = $w.R
        $key2 = "$($r2.L),$($r2.T),$($r2.R),$($r2.B)"
        if ($lastWins[$k] -ne $key2) {
            [System.IO.File]::AppendAllText($log, "WIN $now $($k.ToString('x')) $key2`n")
            $lastWins[$k] = $key2
        }
    }
    Start-Sleep -Milliseconds 16
}
[System.IO.File]::AppendAllText($log, "END $([Environment]::TickCount)`n")
Write-Host "monitor done, log: $log"
