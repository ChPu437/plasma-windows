# ============================================================================
# sync-blueprints.ps1 - keep Craft blueprint patch copies in sync with patches/
#
# Craft applies patches from the blueprint directory copies
# (CraftRoot\etc\blueprints\locations\craft-blueprints-kde\...), NOT from
# this repo's patches/. This script compares the two and reports (or
# copies) any differences.
#
# Usage:
#   powershell -File tools\sync-blueprints.ps1            # report only
#   powershell -File tools\sync-blueprints.ps1 -Sync       # copy + report
#   powershell -File tools\sync-blueprints.ps1 -CraftRoot D:\Projects\CraftRoot
#
# Exit code 0 = in sync (or sync completed), 1 = differences found (report mode).
# ============================================================================

param(
    [switch]$Sync,
    [string]$CraftRoot = "D:\Projects\CraftRoot",
    [string]$PatchesDir = "$PSScriptRoot\..\patches"
)

$ErrorActionPreference = "Stop"

# patches\<component>\<file> -> blueprint-relative path (under locations\craft-blueprints-kde)
$Map = @{
    "kconfig"                          = "kde\frameworks\tier1\kconfig"
    "kcoreaddons"                      = "kde\frameworks\tier1\kcoreaddons"
    "kwindowsystem"                    = "kde\frameworks\tier1\kwindowsystem"
    "kauth"                            = "kde\frameworks\tier2\kauth"
    "kded"                             = "kde\frameworks\tier3\kded"
    "kiconthemes"                      = "kde\frameworks\tier3\kiconthemes"
    "kio"                              = "kde\frameworks\tier3\kio"
    "krunner"                          = "kde\frameworks\tier3\krunner"
    "libplasma"                        = "kde\plasma\libplasma"
    "plasma-workspace"                 = "kde\plasma\plasma-workspace"
    "plasma-desktop"                   = "kde\plasma\plasma-desktop"
    "plasma5support"                   = "kde\plasma\plasma5support"
    "plasma-activities-stats"          = "kde\plasma\plasma-activities-stats"
    "qtbase"                           = "libs\qt6\qtbase\.craft"
}

$BlueprintRoot = Join-Path $CraftRoot "etc\blueprints\locations\craft-blueprints-kde"
if (-not (Test-Path $BlueprintRoot)) {
    Write-Error "Blueprint root not found: $BlueprintRoot (check -CraftRoot)"
}

$diffs = @()
$copied = @()
$missing = @()

foreach ($component in $Map.Keys) {
    $bpRel = $Map[$component]
    $srcDir = Join-Path $PatchesDir $component
    if (-not (Test-Path $srcDir)) {
        Write-Warning "patches\$component not found - skipped"
        continue
    }
    foreach ($patch in Get-ChildItem $srcDir -Filter "*.patch") {
        $dst = Join-Path $BlueprintRoot (Join-Path $bpRel $patch.Name)
        if (-not (Test-Path $dst)) {
            $missing += "$component\$($patch.Name) -> $bpRel\$($patch.Name) (not in blueprint)"
            continue
        }
        $h1 = (Get-FileHash $patch.FullName -Algorithm SHA256).Hash
        $h2 = (Get-FileHash $dst -Algorithm SHA256).Hash
        if ($h1 -ne $h2) {
            $diffs += "$component\$($patch.Name)"
            if ($Sync) {
                Copy-Item $patch.FullName $dst -Force
                $copied += "$component\$($patch.Name)"
            }
        }
    }
}

Write-Host "patches compared: $((Get-ChildItem $PatchesDir -Recurse -Filter *.patch).Count) files"
Write-Host "blueprint root:   $BlueprintRoot"
if ($missing.Count -gt 0) {
    Write-Host "`n[missing in blueprint]"
    $missing | ForEach-Object { Write-Host "  $_" }
}
if ($diffs.Count -gt 0 -and -not $Sync) {
    Write-Host "`n[different - run with -Sync to copy]"
    $diffs | ForEach-Object { Write-Host "  $_" }
    exit 1
} elseif ($diffs.Count -gt 0) {
    Write-Host "`n[synced]"
    $copied | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Host "`nAll patches in sync."
}
exit 0
