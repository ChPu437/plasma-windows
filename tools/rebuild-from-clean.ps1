# ============================================================================
# rebuild-from-clean.ps1 - force a craft rebuild of one package from the
# clean tarball (reapplies all patches), then installs via ninja short path.
#
# Craft normally reports "up to date" even when patch files changed; this
# script deletes the work tree + image dirs so unpack -> patch -> build
# runs for real. This is the authoritative patch validation.
#
# Usage:
#   powershell -File tools\rebuild-from-clean.ps1 -Package libplasma
#   powershell -File tools\rebuild-from-clean.ps1 -Package kde/plasma/libplasma
#   powershell -File tools\rebuild-from-clean.ps1 -Package libplasma -SkipNinja
#
# Exit code 0 = craft unpack+patch+configure ok AND ninja install ok.
# ============================================================================

param(
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$CraftRoot = "D:\Projects\CraftRoot",
    [string]$VcVars64 = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    [switch]$SkipNinja,
    [switch]$SyncBlueprintsFirst
)

$ErrorActionPreference = "Stop"

if ($SyncBlueprintsFirst) {
    Write-Host "== syncing blueprints first =="
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\sync-blueprints.ps1" -Sync
    if ($LASTEXITCODE -ne 0) { Write-Error "sync-blueprints failed" }
}

# --- locate the package build dir -------------------------------------------
$rel = $Package.Trim('/').Split('/')[-1]
$candidate = Join-Path $CraftRoot "build\$($Package.Trim('/').Replace('/','\'))"
if (-not (Test-Path $candidate)) {
    $found = Get-ChildItem $CraftRoot\build -Recurse -Directory -Filter $rel -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\work\\' -and $_.FullName -notmatch '\\work$' } | Select-Object -First 1
    if (-not $found) { Write-Error "cannot locate build dir for package $Package" }
    $candidate = $found.FullName
}
Write-Host "package dir: $candidate"

# --- remove work tree + image dirs ------------------------------------------
foreach ($d in @("work", "image-RelWithDebInfo-*", "image-*-dbg")) {
    Get-ChildItem $candidate -Directory -Filter $d -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "deleting $($_.FullName)"
        Remove-Item $_.FullName -Recurse -Force
    }
}

# --- craft rebuild ----------------------------------------------------------
$cmd = "& '$CraftRoot\craft\craftenv.ps1'; craft --ignoreInstalled --no-cache $Package 2>&1 | Out-String"
Write-Host "== craft --ignoreInstalled --no-cache $Package =="
$out = cmd /c "powershell -NoProfile -ExecutionPolicy Bypass -Command `"$cmd`""
$out | Select-String "failed|FAILED|applying patch|up to date" | ForEach-Object { Write-Host "  $_" }
if ($out -match "all failed|FAILED") {
    Write-Error "craft rebuild failed - see output above"
}
if ($SkipNinja) {
    Write-Host "craft unpack+patch+configure ok (ninja skipped)."
    exit 0
}

# --- ninja install via the craft short path ---------------------------------
$junction = Get-ChildItem "D:\_" -Directory -ErrorAction SilentlyContinue |
    Where-Object { (Get-Item $_.FullName -ErrorAction SilentlyContinue).LinkType -eq "Junction" -and (cmd /c "fsutil reparsepoint query `"$($_.FullName)`" 2>&1 | Select-String $candidate) } | Select-Object -First 1
if (-not $junction) {
    Write-Error "cannot find craft short-path junction for $candidate under D:\_"
}
$ninjaBuild = Join-Path $junction.FullName "build"
Write-Host "ninja build dir: $ninjaBuild"

$craftDev = "$CraftRoot\dev-utils\bin"
$craftBin = "$CraftRoot\bin"
$ninja = "$CraftRoot\dev-utils\bin\ninja.exe"
$env:Path = "$craftDev;$craftBin;$env:Path"
$installOut = cmd /c "call `"$VcVars64`" >nul 2>&1 && set PATH=$craftDev;$craftBin;%PATH% && $ninja -C $ninjaBuild install -j 12 2>&1"
$installOut | Select-String "error|FAILED" | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) {
    Write-Error "ninja install failed"
}
Write-Host "rebuild-from-clean: OK"
exit 0


