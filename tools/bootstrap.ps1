# ============================================================================
# bootstrap.ps1 - verify a Plasma Windows dev environment is reproducible
#
# For a fresh checkout (or a fresh machine) this:
#   1. checks the toolchain (craft, VS, upstream-versions.json)
#   2. checks that upstream-versions.json matches the patches/ collection
#   3. syncs the patch copies into the Craft blueprints
#   4. runs the static patch format checks
#   5. prints the craft commands to build/install the components
#
# It does NOT build anything (a full craft install of the dependency tree
# is the next manual step - see the printed commands).
#
# Usage:
#   powershell -File tools\bootstrap.ps1
#   powershell -File tools\bootstrap.ps1 -CraftRoot D:\Projects\CraftRoot -Sync
# ============================================================================

param(
    [string]$CraftRoot = "D:\Projects\CraftRoot",
    [switch]$Sync
)

$ErrorActionPreference = "Stop"
$ok = $true

Write-Host "== Plasma Windows bootstrap check =="

# --- 1. toolchain ------------------------------------------------------------
Write-Host "`n[1] toolchain"
foreach ($check in @(
    @{ Name = "craft"; Path = "$CraftRoot\craft\craftenv.ps1" },
    @{ Name = "blueprints"; Path = "$CraftRoot\etc\blueprints\locations\craft-blueprints-kde" },
    @{ Name = "patch.exe"; Path = "$CraftRoot\dev-utils\bin\patch.exe" },
    @{ Name = "cmake"; Path = "$CraftRoot\dev-utils\bin\cmake.exe" }
)) {
    $hit = Test-Path $check.Path
    Write-Host "  $($check.Name): $(if ($hit) { 'ok' } else { 'MISSING' })"
    if (-not $hit) { $ok = $false }
}

# --- 2. upstream-versions.json vs patches/ -----------------------------------
Write-Host "`n[2] upstream-versions.json vs patches/"
$versions = Get-Content "$PSScriptRoot\..\upstream-versions.json" -Raw | ConvertFrom-Json
$mismatch = @()
foreach ($comp in $versions.components.PSObject.Properties) {
    $srcDir = Join-Path "$PSScriptRoot\..\patches" $comp.Name
    if (-not (Test-Path $srcDir)) { $mismatch += "$($comp.Name): patches dir missing"; continue }
    foreach ($p in $comp.Value.patches) {
        if (-not (Test-Path (Join-Path $srcDir $p))) { $mismatch += "$($comp.Name): $p listed but missing" }
    }
}
if ($mismatch.Count -eq 0) {
    Write-Host "  $($versions.components.PSObject.Properties.Count) components, all patch lists match"
} else {
    $mismatch | ForEach-Object { Write-Host "  MISMATCH: $_" }
    $ok = $false
}

# --- 3. sync blueprints ------------------------------------------------------
Write-Host "`n[3] blueprint sync"
$syncArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "$PSScriptRoot\sync-blueprints.ps1")
if ($Sync) { $syncArgs += "-Sync" }
& powershell @syncArgs
if ($LASTEXITCODE -ne 0 -and -not $Sync) { Write-Host "  (re-run with -Sync to copy)" }

# --- 4. patch format checks ---------------------------------------------------
Write-Host "`n[4] patch format checks"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\verify-patches.ps1"
if ($LASTEXITCODE -ne 0) { $ok = $false }

# --- 5. next steps ------------------------------------------------------------
Write-Host "`n[5] build commands (run in a craft shell, or via tools\rebuild-from-clean.ps1)"
$components = ($versions.components.PSObject.Properties | ForEach-Object { $_.Name }) -join " "
Write-Host "  craft --install-deps $components"
Write-Host "  craft --no-cache $components"
Write-Host "  (or per package: powershell -File tools\rebuild-from-clean.ps1 -Package <name>)"

Write-Host ""
if ($ok) {
    Write-Host "Bootstrap check OK." -ForegroundColor Green
    exit 0
}
Write-Host "Bootstrap check FAILED - fix the reported items." -ForegroundColor Red
exit 1
