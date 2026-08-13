# ============================================================================
# update-vm-shared.ps1 - sync the CraftRoot runtime + scripts to the VMware
# shared folder (replaces the ad-hoc manual robocopy)
#
# Usage:
#   powershell -File tools\update-vm-shared.ps1              # report only
#   powershell -File tools\update-vm-shared.ps1 -Sync         # copy
#   powershell -File tools\update-vm-shared.ps1 -SharedDir D:\documents\shared\plasma-vm
#
# Copies:
#   1. the CraftRoot runtime (bin/lib/plugins/qml/share/etc., excluding
#      build trees, downloads and dev tools - same rules as make-vm-package.cmd)
#   2. the session scripts (deploy-vm.cmd, session-shell.cmd, plasma-shell.cmd,
#      plasma-common.cmd, start-plasma-session.cmd) + dbus-session-plasma.conf
# ============================================================================

param(
    [switch]$Sync,
    [string]$CraftRoot = "D:\Projects\CraftRoot",
    [string]$SharedDir = "D:\documents\shared\plasma-vm",
    [string]$ToolsDir = "$PSScriptRoot"
)

$ErrorActionPreference = "Stop"

$excludeDirs = @("build", "download", "dev-utils", "logs", "doc", "html", "man", "tests", "msys", "include", "certs", "var", "translations")

Write-Host "CraftRoot : $CraftRoot"
Write-Host "SharedDir : $SharedDir"
Write-Host "Mode      : $(if ($Sync) { 'SYNC' } else { 'report' })"

if (-not (Test-Path $SharedDir)) {
    if ($Sync) { New-Item -ItemType Directory -Path $SharedDir -Force | Out-Null }
    else { Write-Warning "SharedDir missing: $SharedDir (will be created with -Sync)"; exit 1 }
}

# --- 1. runtime tree ---------------------------------------------------------
$robocopyArgs = @($CraftRoot, $SharedDir, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP")
foreach ($d in $excludeDirs) { $robocopyArgs += "/XD"; $robocopyArgs += $d }
$robocopyArgs += "/XF"; $robocopyArgs += "*.pdb"

Write-Host "`n== runtime tree =="
$rc = & robocopy @robocopyArgs
Write-Host "robocopy exit: $LASTEXITCODE (0-7 = ok, 8+ = failure)"
if ($LASTEXITCODE -ge 8) { Write-Error "robocopy failed with code $LASTEXITCODE" }

# --- 2. scripts + dbus conf --------------------------------------------------
Write-Host "`n== scripts =="
$scripts = @("deploy-vm.cmd", "session-shell.cmd", "plasma-shell.cmd", "plasma-common.cmd", "start-plasma-session.cmd", "switch-shell.cmd", "shell-registry.ps1", "update-from-shared.cmd")
foreach ($s in $scripts) {
    $src = Join-Path $ToolsDir $s
    if (Test-Path $src) {
        $dst = Join-Path $SharedDir $s
        $h1 = (Get-FileHash $src -Algorithm SHA256).Hash
        $h2 = if (Test-Path $dst) { (Get-FileHash $dst -Algorithm SHA256).Hash } else { "" }
        if ($h1 -ne $h2) {
            Write-Host "  $s -> $(if ($Sync) { 'copying' } else { 'DIFFERS' })"
            if ($Sync) { Copy-Item $src $dst -Force }
        } else {
            Write-Host "  $s -> same"
        }
    } else {
        Write-Host "  $s -> (not in tools/)"
    }
}
$dbusSrc = Join-Path $ToolsDir "dbus\session-plasma.conf"
if (Test-Path $dbusSrc) {
    Copy-Item $dbusSrc (Join-Path $SharedDir "dbus-session-plasma.conf") -Force -ErrorAction SilentlyContinue
    Write-Host "  dbus-session-plasma.conf -> copied"
}

Write-Host "`ndone. Remember: bin\data\wallpapers\Next (the wallpaper package) is part of the runtime tree and comes along automatically."
