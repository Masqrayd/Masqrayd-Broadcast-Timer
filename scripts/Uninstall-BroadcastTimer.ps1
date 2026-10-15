param([string]$PluginRoot = "$env:ProgramData\obs-studio\plugins")
$ErrorActionPreference = 'Stop'
try {
    if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS before uninstalling the timer.' }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -PluginRoot "{1}"' -f $PSCommandPath, $PluginRoot
        $process = Start-Process -FilePath "$PSHOME\powershell.exe" -ArgumentList $arguments -Verb RunAs -Wait -PassThru
        exit $process.ExitCode
    }
    $destination = Join-Path $PluginRoot 'masqrayd-broadcast-timer'
    if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
    Write-Host 'Timer plugin removed. Your saved settings were retained.'
    Read-Host 'Press Enter to close'
}
catch { Write-Host $_.Exception.Message -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
