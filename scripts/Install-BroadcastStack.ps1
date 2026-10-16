param([string]$PluginRoot = "$env:ProgramData\obs-studio\plugins", [switch]$NoPause)
$ErrorActionPreference = 'Stop'
try {
    if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS before installing Broadcast Stack, then run INSTALL.cmd again.' }
    $plugins = @('masqrayd-broadcast-timer', 'scene-vault', 'camera-popout-native', 'masqrayd-scene-collections')
    foreach ($name in $plugins) {
        if (!(Test-Path (Join-Path $PSScriptRoot "$name\bin\64bit\$name.dll"))) {
            throw "Missing compiled $name DLL. Extract the entire Windows package first."
        }
        $legacy = Join-Path $env:ProgramFiles "obs-studio\obs-plugins\64bit\$name.dll"
        if (Test-Path $legacy) { throw "A manually installed duplicate exists at $legacy. Move that copy outside OBS before running this installer." }
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -PluginRoot "{1}"' -f $PSCommandPath, $PluginRoot
        if ($NoPause) { $arguments += ' -NoPause' }
        $process = Start-Process -FilePath "$PSHOME\powershell.exe" -ArgumentList $arguments -Verb RunAs -Wait -PassThru
        exit $process.ExitCode
    }
    New-Item -ItemType Directory -Path $PluginRoot -Force | Out-Null
    $backup = Join-Path $env:ProgramData ('Masqrayd\BroadcastStackBackups\' + [guid]::NewGuid().ToString())
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $copied = @(); $moved = @()
    try {
        foreach ($name in $plugins) {
            $destination = Join-Path $PluginRoot $name
            if (Test-Path $destination) {
                Move-Item -LiteralPath $destination -Destination (Join-Path $backup $name)
                $moved += $name
            }
            $copied += $name
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination -Recurse
        }
    }
    catch {
        foreach ($name in $copied) {
            $destination = Join-Path $PluginRoot $name
            if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
        }
        foreach ($name in $moved) { Move-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $PluginRoot $name) }
        throw
    }
    Write-Host 'Installed. Open OBS > Tools > Masqrayd Broadcast Stack, then choose Panels.' -ForegroundColor Green
    Write-Host 'Camera, scene controller, timer, and scene collection preferences were retained.'
    Write-Host "Previous plugin files are backed up at $backup"
    if (!$NoPause) { Read-Host 'Press Enter to close' }
}
catch { Write-Host $_.Exception.Message -ForegroundColor Red; if (!$NoPause) { Read-Host 'Press Enter to close' }; exit 1 }
