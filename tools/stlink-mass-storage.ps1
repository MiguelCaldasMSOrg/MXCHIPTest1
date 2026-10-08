#requires -Version 7.2
<#
.SYNOPSIS
Prepares ST's updater or reversibly switches the ST-Link/V2-1 mass-storage personality.
.DESCRIPTION
This is a BOARD-SIDE firmware operation, not Windows device hiding.
Status and WhatIf never invoke the firmware updater. Setup verifies a supplied
official STSW-LINK007 archive (or attempts an official download), then copies
only ST's signed JAR and x64 driver to a private local directory.
Enabled/Disabled require confirmation and use only the reversible mscOnOpt/
mscOffOpt settings. The vendor process is never killed automatically.
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = "High")]
param(
  [ValidateSet("Status", "Setup", "Enabled", "Disabled")][string]$Action = "Status",
  [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$SerialNumber,
  [string]$ToolDirectory,
  [string]$VendorZip,
  [switch]$DownloadVendorTool,
  [switch]$AcceptVendorLicense,
  [string]$JavaPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -DisableNameChecking -Force
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw "Run this script in 64-bit PowerShell 7.2+ on Windows." }
if (-not $ToolDirectory) { $ToolDirectory = Join-Path $env:LOCALAPPDATA "MXCHIPTest1\STLinkUpgrade" }
$ToolDirectory = [IO.Path]::GetFullPath($ToolDirectory)

if ($Action -eq "Setup") {
  if (-not $AcceptVendorLicense) { throw "Obtain/accept STSW-LINK007's license from ST, then specify -AcceptVendorLicense. Vendor binaries are not distributed by this repository." }
  if ([bool]$VendorZip -eq [bool]$DownloadVendorTool) { throw "Specify exactly one of -VendorZip or -DownloadVendorTool." }
  $null = Assert-MxLocalDirectory -Path $ToolDirectory -ForbiddenRoot (Split-Path -Parent $PSScriptRoot)
  if (Test-Path -LiteralPath $ToolDirectory) { throw "ToolDirectory already exists. Use a new directory to prepare a different vendor version." }
  if (-not $PSCmdlet.ShouldProcess($ToolDirectory, "Prepare authenticated ST vendor updater files; no device changes")) { return }
  New-MxPrivateDirectory $ToolDirectory
  $archivePath = $VendorZip
  $downloadPath = Join-Path $ToolDirectory "vendor-download.zip"
  try {
    if ($DownloadVendorTool) {
      Write-Output "Downloading from ST only. If the server requires login or is unavailable, supply the official ZIP with -VendorZip."
      Invoke-WebRequest -Uri "https://www.st.com/resource/en/firmware/stsw-link007.zip" -OutFile $downloadPath -TimeoutSec 90
      $archivePath = $downloadPath
    }
    $archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $archivePath).Path)
    try {
      $jars = @($archive.Entries | Where-Object { $_.FullName -match '(^|/)AllPlatforms/STLinkUpgrade\.jar$' })
      if ($jars.Count -ne 1) { throw "Expected one AllPlatforms/STLinkUpgrade.jar in the official package." }
      $prefix = $jars[0].FullName.Substring(0, $jars[0].FullName.Length - "STLinkUpgrade.jar".Length)
      $jarBytes = Read-MxZipEntry $archive $jars[0].FullName
      $dllBytes = Read-MxZipEntry $archive ($prefix + "native/win_x64/STLinkUSBDriver.dll")
      $native = Join-Path $ToolDirectory "native\win_x64"
      $null = New-Item -Path $native -ItemType Directory -Force
      [IO.File]::WriteAllBytes((Join-Path $ToolDirectory "STLinkUpgrade.jar"), $jarBytes)
      [IO.File]::WriteAllBytes((Join-Path $native "STLinkUSBDriver.dll"), $dllBytes)
    } finally {
      $archive.Dispose()
    }
    $package = Test-MxUpdaterPackage $ToolDirectory
    $package | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ToolDirectory "verified-package.json") -Encoding utf8
    Write-Output "Authenticated ST updater prepared at $ToolDirectory. No probe was opened or modified."
    Write-Output "JAR SHA-256: $($package.JarSha256)"
  } catch {
    foreach ($relative in @("STLinkUpgrade.jar", "native\win_x64\STLinkUSBDriver.dll", "verified-package.json")) {
      $file = Join-Path $ToolDirectory $relative
      if (Test-Path -LiteralPath $file -PathType Leaf) { Remove-Item -LiteralPath $file -Force }
    }
    Write-Warning "Setup failed; no vendor executable was run. The private tool directory is retained. Use a new -ToolDirectory when retrying."
    throw
  } finally {
    if ($DownloadVendorTool -and (Test-Path -LiteralPath $downloadPath -PathType Leaf)) { Remove-Item -LiteralPath $downloadPath -Force }
  }
  return
}
if ($VendorZip -or $DownloadVendorTool -or $AcceptVendorLicense) { throw "Vendor package options apply only to -Action Setup." }
$probe = Get-MxStLink -SerialNumber $SerialNumber
$probe | Format-List
if ($Action -eq "Status") { return }
if (Test-MxMscState $probe $Action) {
  Write-Output "Mass storage is already $($Action.ToLowerInvariant()); no firmware write needed."
  return
}
$arguments = @(New-MxMscArguments -SerialNumber $probe.SerialNumber -State $Action)
Write-Output "Vendor operation: STLinkUpgrade.jar $($arguments -join ' ')"
Write-Warning "This reprograms the ST-Link coprocessor with the selected vendor bundle and may reset/disconnect USB. It does not intentionally program the target application or STSAFE. Keep USB power connected and close debugger/serial tools."
if ($Action -eq "Disabled") {
  Write-Warning "No-MSC firmware may enumerate as PID 3752. The core's OpenOCD 0.10 cannot use that personality; reenable MSC with this tool or use a newer compatible OpenOCD."
}
if (-not $PSCmdlet.ShouldProcess($probe.SerialNumber, "Set board-side mass storage to $Action by programming ST-Link firmware")) { return }
$package = Test-MxUpdaterPackage $ToolDirectory
if (-not $JavaPath) { $JavaPath = (Get-Command java -CommandType Application -ErrorAction Stop).Source }
$JavaPath = (Resolve-Path -LiteralPath $JavaPath).Path
$javaArguments = @("-Djava.awt.headless=true", "-jar", $package.Jar)
$preflight = @(New-MxMscArguments -SerialNumber $probe.SerialNumber -State $Action -Preflight)
$result = Invoke-MxTool $JavaPath ($javaArguments + $preflight) $ToolDirectory -TimeoutSeconds 45
Assert-MxNativeSuccess $result "ST updater parameter preflight"
if ($result.Text -match '(?i)(parameter error|command syntax error|incompatible|no st-link|failure)') {
  throw "ST updater rejected the selected probe or options.`n$($result.Text)"
}
Write-Output $result.Text.Trim()
# Recheck identity and package immediately before the only firmware-writing invocation.
$null = Get-MxStLink -SerialNumber $probe.SerialNumber
$confirmedPackage = Test-MxUpdaterPackage $ToolDirectory
if ($confirmedPackage.JarSha256 -ne $package.JarSha256 -or $confirmedPackage.DriverSha256 -ne $package.DriverSha256) {
  throw "Vendor package changed during preflight."
}
$result = Invoke-MxTool $JavaPath ($javaArguments + $arguments) $ToolDirectory -NeverKill
Assert-MxNativeSuccess $result "ST-Link firmware switch"
Write-Output $result.Text.Trim()
$deadline = [DateTime]::UtcNow.AddSeconds(45)
$stable = 0
$lastProblem = "USB did not re-enumerate"
while ([DateTime]::UtcNow -lt $deadline) {
  Start-Sleep -Milliseconds 750
  try {
    $after = Get-MxStLink -SerialNumber $probe.SerialNumber
    if (Test-MxMscState $after $Action) {
      $stable++
      if ($stable -ge 2) {
        Write-Output "Verified: board-side mass storage $($Action.ToLowerInvariant()); the same ST-Link serial, debug interface, and serial port re-enumerated."
        Write-Output "USB PID now $($after.UsbPid). No Windows devices, drive-letter rules, or automount settings were changed."
        return
      }
    } else {
      $stable = 0
      $lastProblem = "The selected device's interface set or driver status does not match $Action."
    }
  } catch {
    $stable = 0
    $lastProblem = $_.Exception.Message
  }
}
throw "The updater returned, but the requested USB state was NOT verified: $lastProblem. Inspect STLinkUpgrade and reconnect only after it has finished. Do not repeat firmware writes blindly."
