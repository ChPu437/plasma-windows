# Build .desktop bridge files for the Windows Start Menu so KDE's
# launcher (kickoff) lists native Windows applications.
#
# Scans the per-user and per-machine Start Menu "Programs" folders for
# .lnk shortcuts, resolves each target with WScript.Shell, and writes a
# .desktop file per shortcut into <LOCALAPPDATA>\applications\.
# Afterwards ksycoca must be rebuilt (kbuildsycoca6) for KService to see
# them - this script does that too when -RebuildSycoca is given.

param(
    [switch]$RebuildSycoca,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

$outDir = Join-Path $env:LOCALAPPDATA "applications"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$startMenuDirs = @(
    (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"),
    (Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs")
)

$shell = New-Object -ComObject WScript.Shell

$count = 0
foreach ($dir in $startMenuDirs) {
    if (-not (Test-Path $dir)) {
        continue
    }
    Get-ChildItem -Path $dir -Filter "*.lnk" -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
        $link = $_
        try {
            $shortcut = $shell.CreateShortcut($link.FullName)
            $target = $shortcut.TargetPath
            $args = $shortcut.Arguments
        } catch {
            return
        }
        if (-not $target -or -not (Test-Path $target)) {
            return  # dangling or system shortcut
        }
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($link.Name)
        # Skip obvious maintenance entries (unicode escapes avoid script
        # encoding issues under Windows PowerShell 5.1)
        if ($baseName -match "Uninstall|Readme|README|Help|Update|\u5378\u8f7d") {
            return
        }
        $ext = [System.IO.Path]::GetExtension($target).ToLower()
        if ($ext -notin @(".exe", ".bat", ".cmd", ".com")) {
            return
        }
        # Stable file name: sanitized app name + hash of the target
        $hash = [System.BitConverter]::ToString([System.Security.Cryptography.MD5]::Create().ComputeHash(
            [System.Text.Encoding]::UTF8.GetBytes($link.FullName))).Replace("-", "").Substring(0, 8).ToLower()
        $safeName = ($baseName -replace '[^\w\-\. ]', "_").Trim()
        $desktopFile = Join-Path $outDir ("windows-startmenu-{0}-{1}.desktop" -f $safeName, $hash)

        # Escape the Exec line: quote paths containing spaces
        $execTarget = if ($target -match " ") { '"' + $target + '"' } else { $target }
        $execArgs = if ($args) { " " + $args } else { "" }

        $content = @"
[Desktop Entry]
Type=Application
Name=$baseName
Name[$([System.Globalization.CultureInfo]::CurrentCulture.Name)]=$baseName
Comment=Windows application
Exec=$execTarget$execArgs
Path=$(Split-Path $target -Parent)
Icon=$target
Terminal=false
StartupNotify=false
Categories=Utility;
"@
        [System.IO.File]::WriteAllText($desktopFile, $content, [System.Text.UTF8Encoding]::new($false))
        $count++
        if (-not $Quiet) {
            Write-Host "desktop: $baseName -> $target"
        }
    }
}

Write-Host "Generated $count .desktop bridge files in $outDir"

if ($RebuildSycoca) {
    $kbuildsycoca = Join-Path $env:ProgramFiles "..\..\Projects\CraftRoot\bin\kbuildsycoca6.exe"
    if (-not (Test-Path $kbuildsycoca)) {
        $kbuildsycoca = "D:\Projects\CraftRoot\bin\kbuildsycoca6.exe"
    }
    if (Test-Path $kbuildsycoca) {
        & $kbuildsycoca --noincremental 2>&1 | Out-Null
        Write-Host "ksycoca rebuilt"
    } else {
        Write-Warning "kbuildsycoca6 not found - run it manually after this script"
    }
}
