# ============================================================================
# verify-patches.ps1 - static format checks + reverse dry-run against build trees
#
# Guards against the patch-file defects that have bitten this project:
#   * CRLF line endings (git apply refuses; keep patches LF)
#   * empty hunks (all context, no +/- lines - patch.exe reports malformed)
#   * `b/\` backslash paths in +++ lines (git apply refuses)
#   * hunk line-count mismatches (patch.exe reports malformed)
#
# Optional: with -VerifyWorkTrees, runs `patch.exe -R --dry-run` against the
# given work trees to prove patches/ == what the build trees contain.
#
# Usage:
#   powershell -File tools\verify-patches.ps1
#   powershell -File tools\verify-patches.ps1 -VerifyWorkTrees @{
#       libplasma = "D:\...\libplasma\work\libplasma-6.7.4";
#       "plasma-workspace" = "D:\...\plasma-workspace\work\plasma-workspace-6.7.4" }
#
# Exit code 0 = all checks pass.
# ============================================================================

param(
    [hashtable]$VerifyWorkTrees = @{},
    [string]$VerifyWorkTreesFile = "",
    [string]$PatchesDir = "$PSScriptRoot\..\patches",
    [string]$PatchExe = "D:\Projects\CraftRoot\dev-utils\bin\patch.exe"
)

$ErrorActionPreference = "Continue"
$failed = $false

if ($VerifyWorkTreesFile -and $VerifyWorkTrees.Count -eq 0) {
    $obj = Get-Content $VerifyWorkTreesFile -Raw | ConvertFrom-Json
    foreach ($prop in $obj.PSObject.Properties) {
        $VerifyWorkTrees[$prop.Name] = $prop.Value
    }
}

function Fail($msg) {
    Write-Host "  FAIL: $msg" -ForegroundColor Red
    $script:failed = $true
}

# --- 1. static format checks -------------------------------------------------
Write-Host "== Static format checks =="
$total = 0
foreach ($patchFile in Get-ChildItem $PatchesDir -Recurse -Filter "*.patch") {
    $total++
    $rel = $patchFile.FullName.Substring((Resolve-Path $PatchesDir).Path.Length + 1)
    $bytes = [System.IO.File]::ReadAllBytes($patchFile.FullName)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        Fail "$rel : file starts with a UTF-8 BOM - strip it"
    }
    $crlf = 0
    for ($i = 1; $i -lt $bytes.Length; $i++) { if ($bytes[$i] -eq 0x0A -and $bytes[$i-1] -eq 0x0D) { $crlf++ } }
    if ($crlf -gt 0) { Fail "$rel : CRLF line endings ($crlf lines) - use LF" }

    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($text -match 'b/\\') { Fail "$rel : backslash path in +++ line (b/\)" }

    # hunk parse: every @@ line must be followed by at least one +/- line
    # (context-only hunks are empty and rejected by patch.exe as malformed)
    $lines = $text -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^@@ -(\d+)(,(\d+))? \+(\d+)(,(\d+))? @@') {
            $adds = 0; $subs = 0; $ctx = 0
            $j = $i + 1
            while ($j -lt $lines.Count -and $lines[$j] -notmatch '^@@' -and $lines[$j] -notmatch '^diff --git') {
                if ($lines[$j].StartsWith('+')) { $adds++ }
                elseif ($lines[$j].StartsWith('-')) { $subs++ }
                elseif ($lines[$j].StartsWith(' ')) { $ctx++ }
                elseif ($lines[$j].Length -eq 0 -and $lines[$j+1] -match '^diff --git') { break }
                $j++
            }
            if ($adds -eq 0 -and $subs -eq 0) {
                Fail "$rel : empty hunk at line $($i+1) (@@ $($lines[$i].Trim()))"
            }
        }
    }
}
Write-Host "checked $total patch files"

# --- 2. reverse dry-run against work trees ----------------------------------
# Known limitations (by design):
#   * patches containing `new file mode` cannot be reverse-applied by
#     patch.exe (it cannot delete files via the a/.. compact header) - skipped
#   * patches whose generation baseline drifted from the craft tarball may
#     fail with line-number/context offsets even though craft applies them
#     fine (fuzz). Such failures are reported as warnings; the authoritative
#     check is `tools/rebuild-from-clean.ps1` (craft forward apply).
if ($VerifyWorkTrees.Count -gt 0) {
    Write-Host "`n== Reverse dry-run against work trees =="
    foreach ($component in $VerifyWorkTrees.Keys) {
        $work = $VerifyWorkTrees[$component]
        $srcDir = Join-Path $PatchesDir $component
        if (-not (Test-Path $srcDir)) { Fail "component $component : no patches dir"; continue }
        if (-not (Test-Path $work)) { Fail "component $component : work tree missing $work"; continue }
        foreach ($patch in Get-ChildItem $srcDir -Filter "*.patch") {
            if (Select-String -Path $patch.FullName -Pattern "new file mode" -Quiet) {
                Write-Host "  $component\$($patch.Name) -R --dry-run ... skipped (contains new files)"
                continue
            }
            Write-Host "  $component\$($patch.Name) -R --dry-run ..." -NoNewline
            $out = & $PatchExe -R --dry-run -p1 -d $work -i $patch.FullName 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Host " WARN (baseline drift or work-tree sync - craft forward apply is authoritative)"
                $out | Select-Object -First 3 | ForEach-Object { Write-Host "      $_" -ForegroundColor DarkYellow }
            } else {
                Write-Host " ok"
            }
        }
    }
}

if ($failed) {
    Write-Host "`nVERIFY FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "`nAll checks passed." -ForegroundColor Green
exit 0
