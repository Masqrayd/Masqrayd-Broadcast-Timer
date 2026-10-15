param([string]$PluginRoot = "$env:ProgramData\obs-studio\plugins")
$ErrorActionPreference = 'Stop'
try {
    if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS before installing the timer, then run INSTALL.cmd again.' }
    $source = Join-Path $PSScriptRoot 'masqrayd-broadcast-timer'
    if (!(Test-Path (Join-Path $source 'bin\64bit\masqrayd-broadcast-timer.dll'))) {
        throw 'Extract the compiled Windows package completely first. The GitHub source ZIP is not an installer.'
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -PluginRoot "{1}"' -f $PSCommandPath, $PluginRoot
        $process = Start-Process -FilePath "$PSHOME\powershell.exe" -ArgumentList $arguments -Verb RunAs -Wait -PassThru
        exit $process.ExitCode
    }
    $destination = Join-Path $PluginRoot 'masqrayd-broadcast-timer'
    $backup = $null
    New-Item -ItemType Directory -Path $PluginRoot -Force | Out-Null
    if (Test-Path $destination) {
        $backupRoot = Join-Path $env:ProgramData 'Masqrayd\BroadcastTimerBackups'
        New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
        $backup = Join-Path $backupRoot ([guid]::NewGuid().ToString())
        Move-Item -LiteralPath $destination -Destination $backup
    }
    try { Copy-Item -LiteralPath $source -Destination $destination -Recurse }
    catch {
        if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
        if ($backup) { Move-Item -LiteralPath $backup -Destination $destination }
        throw
    }
    Write-Host 'Installed. Open OBS > Tools > Masqrayd Broadcast Timer.' -ForegroundColor Green
    if ($backup) { Write-Host "Previous plugin backup: $backup" }
    Read-Host 'Press Enter to close'
}
catch { Write-Host $_.Exception.Message -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
