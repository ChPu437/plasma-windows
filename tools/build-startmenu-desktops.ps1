# Build .desktop bridge files for the Windows Start Menu so KDE's
# launcher (kickoff) lists native Windows applications and UWP (Store)
# applications.
#
# Sources:
#   - .lnk shortcuts under the per-user and per-machine Start Menu
#     "Programs" folders (resolved with WScript.Shell)
#   - UWP apps enumerated with Get-StartApps (AppUserModelIDs)
#
# Custom categories: %LOCALAPPDATA%\plasma\startmenu-categories.json
# overrides the built-in folder->category mapping, e.g.
#   {
#     "folders": { "Games": "Game", "\u6e38\u620f": "Game" },
#     "apps":    { "Visual Studio 2022": "Development" }
#   }
# Matching order: exact app name -> parent folder name -> built-in map
# -> "Utility".
#
# Afterwards ksycoca must be rebuilt (kbuildsycoca6) for KService to see
# them - this script does that too when -RebuildSycoca is given.

param(
    [switch]$RebuildSycoca,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

$outDir = Join-Path $env:LOCALAPPDATA "applications"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# ---------------------------------------------------------------------------
# custom category overrides
# ---------------------------------------------------------------------------
$customFolders = @{}
$customApps = @{}
$catFile = Join-Path $env:LOCALAPPDATA "plasma\startmenu-categories.json"
if (Test-Path $catFile) {
    try {
        $cat = Get-Content $catFile -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($cat.folders) {
            $cat.folders.PSObject.Properties | ForEach-Object { $customFolders[$_.Name] = $_.Value }
        }
        if ($cat.apps) {
            $cat.apps.PSObject.Properties | ForEach-Object { $customApps[$_.Name] = $_.Value }
        }
        if (-not $Quiet) {
            Write-Host "custom categories: $($customFolders.Count) folders, $($customApps.Count) apps"
        }
    } catch {
        Write-Warning "ignoring invalid $catFile : $($_.Exception.Message)"
    }
}

$builtinMap = @{
    "game"        = "Game"
    "games"       = "Game"
    "\u5de5\u5177"        = "Utility"
    "\u9644\u4ef6"        = "Utility"
    "accessories" = "Utility"
    "utilities"   = "Utility"
    "utility"     = "Utility"
    "office"      = "Office"
    "\u529e\u516c"        = "Office"
    "\u6548\u7387"        = "Office"
    "development" = "Development"
    "developer"   = "Development"
    "\u5f00\u53d1"        = "Development"
    "internet"    = "Network"
    "network"     = "Network"
    "\u6d4f\u89c8\u5668"      = "Network"
    "browser"     = "Network"
    "media"       = "AudioVideo"
    "audio"       = "AudioVideo"
    "video"       = "AudioVideo"
    "music"       = "AudioVideo"
    "\u64ad\u653e"        = "AudioVideo"
    "\u7cfb\u7edf"        = "System"
    "system"      = "System"
    "\u7ba1\u7406"        = "System"
    "administrative" = "System"
    "graphics"    = "Graphics"
    "image"       = "Graphics"
    "photo"       = "Graphics"
    "\u7ed8\u56fe"        = "Graphics"
    "education"   = "Education"
}

function Get-Category([string]$appName, [string]$parentDir) {
    if ($customApps.ContainsKey($appName)) {
        return $customApps[$appName]
    }
    if ($customFolders.ContainsKey($parentDir)) {
        return $customFolders[$parentDir]
    }
    foreach ($key in $builtinMap.Keys) {
        if ($parentDir -match $key -or $appName -match $key) {
            return $builtinMap[$key]
        }
    }
    return "Utility"
}

function Write-DesktopFile([string]$name, [string]$exec, [string]$path, [string]$icon, [string]$category, [string]$stamp) {
    $safeName = ($name -replace '[^\w\-\. ]', "_").Trim()
    $hash = [System.BitConverter]::ToString([System.Security.Cryptography.MD5]::Create().ComputeHash(
        [System.Text.Encoding]::UTF8.GetBytes($stamp))).Replace("-", "").Substring(0, 8).ToLower()
    $desktopFile = Join-Path $outDir ("windows-startmenu-{0}-{1}.desktop" -f $safeName, $hash)
    $content = @"
[Desktop Entry]
Type=Application
Name=$name
Name[$([System.Globalization.CultureInfo]::CurrentCulture.Name)]=$name
Comment=Windows application
Exec=$exec
Path=$path
Icon=$icon
Terminal=false
StartupNotify=false
Categories=$category;
"@
    [System.IO.File]::WriteAllText($desktopFile, $content, [System.Text.UTF8Encoding]::new($false))
    if (-not $Quiet) {
        Write-Host "desktop: $name [$category]"
    }
}

$count = 0

# ---------------------------------------------------------------------------
# 1. classic .lnk shortcuts
# ---------------------------------------------------------------------------
$startMenuDirs = @(
    (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"),
    (Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs")
)

$shell = New-Object -ComObject WScript.Shell

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
        $parentDir = Split-Path (Split-Path $link.FullName -Parent) -Leaf
        $category = Get-Category $baseName $parentDir

        # Escape the Exec line: quote paths containing spaces
        $execTarget = if ($target -match " ") { '"' + $target + '"' } else { $target }
        $execArgs = if ($args) { " " + $args } else { "" }

        Write-DesktopFile $baseName ($execTarget + $execArgs) (Split-Path $target -Parent) $target $category $link.FullName
        $count++
    }
}

# ---------------------------------------------------------------------------
# 2. UWP (Store) applications
# ---------------------------------------------------------------------------
# Get-StartApps returns Name/AppID (AppUserModelID). UWP apps have no
# exe; launch via explorer's shell:AppsFolder with the AUMID.
$uwp = @()
try {
    $uwp = Get-StartApps -ErrorAction Stop
} catch {
    Write-Warning "Get-StartApps unavailable: $($_.Exception.Message)"
}
foreach ($app in $uwp) {
    $name = [string]$app.Name
    if (-not $name) { continue }
    if ($name -match "Uninstall|Readme|Help|\u5378\u8f7d") { continue }
    $aumid = [string]$app.AppID
    if (-not $aumid) { continue }
    # Get-StartApps also lists classic exe entries (AppID = exe path);
    # those already come from the .lnk pass - keep real UWP AUMIDs only
    # (PackageFamily!AppId or "Microsoft.*" forms).
    if ($aumid -match '\.exe$' -or $aumid -match '^[A-Za-z]:') { continue }
    $parentDir = "Apps"
    $category = Get-Category $name $parentDir
    $exec = "explorer.exe `"shell:AppsFolder\$aumid`""
    Write-DesktopFile $name $exec "" "" $category ("uwp:" + $aumid)
    $count++
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
