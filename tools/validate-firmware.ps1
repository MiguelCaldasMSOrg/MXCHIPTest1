param([string]$ApplicationPath)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -DisableNameChecking -Force

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $projectRoot "build"
if (-not $ApplicationPath) {
  $ApplicationPath = Join-Path $buildPath "MXCHIPTest1.ino.bin"
}
$application = [System.IO.File]::ReadAllBytes($applicationPath)
Assert-MxImage -Bytes $application

Write-Output "Validated application-only firmware ($($application.Length) bytes; upload address 0x0800C000)."
