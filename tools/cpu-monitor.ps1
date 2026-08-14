# cpu-monitor.ps1 - sample CPU% of the animation suspects every 500ms.
# Log: CPU <ms> <pid> <name> <cpu%>
param([int]$Seconds = 240)

$log = "C:\Users\jing\AppData\Local\Temp\opencode\cpu-monitor.log"
[System.IO.File]::AppendAllText($log, "START $([Environment]::TickCount)`n")
$names = @("plasmashell", "dwm", "explorer", "titlebar", "notepad", "node", "Code")
$t0 = [Environment]::TickCount
$deadline = $t0 + $Seconds * 1000

function Get-CpuSnapshot {
    $snap = @{}
    foreach ($n in $names) {
        Get-Process $n -ErrorAction SilentlyContinue | ForEach-Object {
            $snap[$_.Id] = [pscustomobject]@{ Name = $_.ProcessName; Cpu = $_.CPU }
        }
    }
    $snap
}

$prev = Get-CpuSnapshot
while ([Environment]::TickCount -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $now = [Environment]::TickCount
    $cur = Get-CpuSnapshot
    $line = @()
    foreach ($id in $cur.Keys) {
        if ($prev.ContainsKey($id)) {
            $dt = ($now - $t0) / 1000.0
            $dCpu = $cur[$id].Cpu - $prev[$id].Cpu
            $pct = [math]::Round(($dCpu / 0.5) * 100.0, 1)
            if ($pct -gt 2) {
                $line += "$($cur[$id].Name)($id)=$pct%"
            }
        }
    }
    if ($line.Count -gt 0) {
        [System.IO.File]::AppendAllText($log, "CPU $now $($line -join ' ')`n")
    }
    $prev = $cur
}
[System.IO.File]::AppendAllText($log, "END $([Environment]::TickCount)`n")
