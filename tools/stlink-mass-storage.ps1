#requires -Version 7.2
<#
.SYNOPSIS
Prepares ST's updater or reversibly switches the ST-Link/V2-1 mass-storage personality.
.DESCRIPTION
This is a BOARD-SIDE firmware operation, not Windows device hiding.
Status and WhatIf never invoke the firmware updater. Setup verifies a supplied
official STSW-LINK007 archive (or attempts an official download), then copies
only ST's signed JAR and x64 driver to a private local directory.
Actual Enabled/Disabled changes request confirmation and UAC elevation when
needed. Bounded, exact-device Windows restarts complete loader transitions.
Only a recognized failure before programming permits one loader continuation;
successful programming is never repeated to exit loader mode. Status, Setup,
WhatIf and an already-correct state do not require elevation. The vendor
process is never killed automatically.
Normal output shows progress and the verified result. Raw diagnostics are
retained in a private operation log; use -Verbose to also display them.
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
. (Join-Path $PSScriptRoot "BoardToolOutput.ps1")
Import-Module (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -DisableNameChecking -Force
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw "Run this script in 64-bit PowerShell 7.2+ on Windows." }
if (-not $ToolDirectory) { $ToolDirectory = Join-Path $env:LOCALAPPDATA "MXCHIPTest1\STLinkUpgrade" }
$ToolDirectory = [IO.Path]::GetFullPath($ToolDirectory)

if ($Action -eq "Setup") {
  if (-not $AcceptVendorLicense) { throw "Review STSW-LINK007's license in drivers\STSW-LINK007-LICENSE.txt or on ST's download page, then specify -AcceptVendorLicense." }
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
  Write-BoardVerified "authenticated ST updater prepared at $ToolDirectory. No board was modified."
  return
}
if ($VendorZip -or $DownloadVendorTool -or $AcceptVendorLicense) { throw "Vendor package options apply only to -Action Setup." }
$probe = Get-MxStLink -SerialNumber $SerialNumber
if ($Action -eq "Status") {
  $probe | Format-List
  return
}
if (Test-MxMscState $probe $Action) {
  Write-BoardVerified "mass storage is already $($Action.ToLowerInvariant()); no firmware write needed."
  return
}
if (-not $WhatIfPreference) {
  foreach ($relative in @("STLinkUpgrade.jar", "native\win_x64\STLinkUSBDriver.dll")) {
    $component = Join-Path $ToolDirectory $relative
    if (-not (Test-Path -LiteralPath $component -PathType Leaf)) {
      throw "ST-Link updater is not prepared: missing '$component'. From the repository root, run .\tools\stlink-mass-storage.ps1 -Action Setup -VendorZip .\drivers\stsw-link007.zip -AcceptVendorLicense after accepting ST's terms, or use -ToolDirectory to select an existing verified AllPlatforms installation. No updater or device write was started."
    }
  }
  if (-not $JavaPath) {
    $java = Get-Command java -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $java) { throw "64-bit Java is required. Install it or supply -JavaPath to its java.exe; no updater was started." }
    $JavaPath = $java.Source
  }
  if (-not (Test-Path -LiteralPath $JavaPath -PathType Leaf)) { throw "Java executable not found: '$JavaPath'. Supply -JavaPath to an installed 64-bit java.exe; no updater was started." }
  $JavaPath = (Resolve-Path -LiteralPath $JavaPath).Path
}
$arguments = @(New-MxMscArguments -SerialNumber $probe.SerialNumber -State $Action)
Write-Verbose "Vendor operation: STLinkUpgrade.jar $($arguments -join ' ')"
Write-Warning "This reprograms the ST-Link coprocessor and may restart its Windows USB device to complete loader entry/exit. USB and the application may reset. It does not program target application flash or STSAFE. Keep USB power connected and close debugger/serial tools."
if ($Action -eq "Disabled") {
  $notice = "No-MSC mode uses USB PID 3752 and may change the COM port. This operation uses ST's vendor updater; it does not invoke OpenOCD or change your Arduino upload configuration."
  if ($WhatIfPreference) { Write-Output $notice } else { Write-Verbose $notice }
}
if (-not $PSCmdlet.ShouldProcess($probe.SerialNumber, "Set board-side mass storage to $Action by programming ST-Link firmware")) { return }
$operation = New-MxMscOperation -State $Action -SerialNumber $probe.SerialNumber -ShowDiagnostics:($VerbosePreference -eq "Continue")
$operation.LastState = "USB PID $($probe.UsbPid), Windows status $($probe.Status)"
Write-Output "Diagnostics: $($operation.LogPath)"
try {
  $progress = if ($Action -eq "Enabled") { "Enabling mass storage..." } else { "Disabling mass storage..." }
  Write-MxMscProgress $operation $progress
  Write-MxMscDiagnostic $operation ("Selected device: " + ($probe | ConvertTo-Json -Compress))
  $operation.Stage = "Validating updater"
  $package = Test-MxUpdaterPackage $ToolDirectory
  Write-MxMscDiagnostic $operation ("Authenticated package: " + ($package | ConvertTo-Json -Compress))
  if (Test-MxAdministrator) {
    $after = Invoke-MxMscWorker -State $Action -SerialNumber $probe.SerialNumber -ToolDirectory $ToolDirectory -JavaPath $JavaPath -Operation $operation
  } else {
    $after = Invoke-MxElevatedMsc -State $Action -SerialNumber $probe.SerialNumber -ToolDirectory $ToolDirectory -JavaPath $JavaPath -Operation $operation
  }
  $operation.Stage = "Complete"
  $operation.LastState = "USB PID $($after.UsbPid), Windows status $($after.Status)"
  Write-MxMscDiagnostic $operation ("Verified result: " + ($after | ConvertTo-Json -Compress))
  Write-BoardVerified "mass storage $($Action.ToLowerInvariant()). Serial: $($after.SerialPortNames -join ', ')."
} catch {
  $reason = $_.Exception.Message
  try {
    Write-MxMscDiagnostic $operation ("TERMINAL ERROR: " + $_.Exception.ToString())
  } catch {
    $reason += " Diagnostic logging also failed: $($_.Exception.Message)"
  }
  throw "Could not set mass storage to $($Action.ToLowerInvariant()).`nStage: $($operation.Stage)`nLast observed state: $($operation.LastState)`nReason: $reason`nDiagnostics: $($operation.LogPath)"
}
