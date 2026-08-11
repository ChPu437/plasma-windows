# shell-registry.ps1 - low-level registry write used by switch-shell.cmd
#
# All inputs arrive via environment variables, so values containing spaces
# and quotes are never mangled by command-line parsing:
#   SHELL_KEY_PS    PowerShell provider path, e.g. HKCU:\Software\...
#   SHELL_VALUE     value to store (install only)
#   SHELL_BACKUP    backup file path (install: previous value, restore: target)
#
# Written to run on Windows PowerShell 2.0 and later (registry cmdlets
# via -Path, file I/O via .NET APIs).
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File shell-registry.ps1 -Action install|restore

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('install', 'restore')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'

if ($Action -eq 'install') {
    $old = $null
    $item = Get-ItemProperty -Path $env:SHELL_KEY_PS -Name Shell -ErrorAction SilentlyContinue
    if ($null -ne $item) {
        $old = $item.Shell
    }
    if ($null -eq $old -or '' -eq $old) {
        $old = 'explorer.exe'
    }
    $backupDir = [System.IO.Path]::GetDirectoryName($env:SHELL_BACKUP)
    [System.IO.Directory]::CreateDirectory($backupDir) | Out-Null
    [System.IO.File]::WriteAllText($env:SHELL_BACKUP, $old)
    Write-Output "Backed up previous shell value: $old"
    New-Item -Path $env:SHELL_KEY_PS -Force | Out-Null
    New-ItemProperty -Path $env:SHELL_KEY_PS -Name Shell -Value $env:SHELL_VALUE -PropertyType String -Force | Out-Null
}
else {
    $prev = 'explorer.exe'
    if ([System.IO.File]::Exists($env:SHELL_BACKUP)) {
        $prev = [System.IO.File]::ReadAllText($env:SHELL_BACKUP)
    }
    Write-Output "Restoring shell: $prev"
    New-Item -Path $env:SHELL_KEY_PS -Force | Out-Null
    New-ItemProperty -Path $env:SHELL_KEY_PS -Name Shell -Value $prev -PropertyType String -Force | Out-Null
}

exit 0
