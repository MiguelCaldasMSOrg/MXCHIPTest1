#requires -Version 7.2
<#
.SYNOPSIS
Installs user-supplied STSAFE host keys and the original SDK's STM32 key loaders.
.DESCRIPTION
Supplied keys only. No application-data access, migration, backup, journal,
resume/restore, random host keys or RDP automation. Does not upload firmware.
An interrupted setup can leave unusable state; this tool provides no repair.
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact="High")]
param(
  [ValidateSet("Status","ProvisionSupplied")][string]$Action = "Status",
  [string]$Port = "COM8",
  [switch]$AcknowledgeUnvalidatedHardware
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "SuppliedKeySetup.psm1") -Force
$serial = $null
$supplied = $null
$pointer = [IntPtr]::Zero
$keys = $null
$keyText = $null
try {
  if ($Action -eq "ProvisionSupplied" -and -not $AcknowledgeUnvalidatedHardware -and -not $WhatIfPreference) {
    throw "Supplied-key setup has not been qualified on personalized hardware. Read docs\LEGACY-PROVISIONING.md and explicitly pass -AcknowledgeUnvalidatedHardware before writing keys."
  }
  if (-not $PSCmdlet.ShouldProcess($Port, "$Action in supplied-key mode 15; no application upload")) { return }
  $serial = Connect-SuppliedKeyTarget $Port
  $status = Get-SuppliedKeyStatus $serial
  if ($Action -eq "Status") {
    $status | Format-List
    return
  }
  if ($status.Rdp -ne 0 -or $status.Pcrop) { throw "Host flash is protected. This tool does not change protection settings." }
  if ($status.HostKeysPresent) { throw "STSAFE host keys already exist. This tool does not replace them or repair an interrupted setup." }
  if (-not $status.HostFlashEmpty) { throw "The reserved STM32 host-key sector is not empty. This tool does not restore or repair it." }
  $supplied = Read-Host "64 hex digits: your 16-byte MAC key followed by your 16-byte cipher key" -AsSecureString
  $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($supplied)
  $keyText = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
  if ($keyText -cnotmatch '^[0-9A-Fa-f]{64}$') { throw "Expected exactly 64 hexadecimal digits; keys were not sent." }
  $keys = [Convert]::FromHexString($keyText)
  Assert-SuppliedHostKeys $keys
  Write-Warning "Only keys and the STM32 key-loader sector will be written. Existing application data is NOT converted or initialized. Keep power connected. An interrupted setup may be unusable; no rollback, resume or repair exists."
  $phrase = "SETKEYS $($status.Uid)"
  if ((Read-Host "Type exactly '$phrase' to proceed") -cne $phrase) { throw "Key setup not confirmed." }
  $null = Read-Host "Press Button A on the board, then press Enter here within 60 seconds"
  $null = Invoke-SuppliedKeyCommand $serial "SK2 SET $($status.Uid) $keyText" '^SK2 INSTALLED$' -TimeoutSeconds 60
  $after = Get-SuppliedKeyStatus $serial
  if ($after.Uid -cne $status.Uid -or -not $after.HostKeysPresent -or -not $after.EnvelopeKeyPresent -or $after.HostFlashEmpty -or $after.Rdp -ne $status.Rdp -or $after.Pcrop -ne $status.Pcrop) {
    throw "The device did not report the expected key state. No repair or automatic retry was attempted."
  }
  Write-Output "Supplied keys and legacy STM32 loaders installed for $($status.Uid). Application data and protection settings were not modified."
} finally {
  if ($keys) { [Array]::Clear($keys,0,$keys.Length) }
  $keyText = $null
  if ($pointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer) }
  if ($supplied) { $supplied.Dispose() }
  if ($serial) {
    try {
      if ($serial.IsOpen) {
        try { $null = Invoke-SuppliedKeyCommand $serial "SK2 ABORT" '^SK2 ABORTED$' -TimeoutSeconds 3 } catch { Write-Warning "Could not clear the device's transient confirmation. No persistent changes were undone." }
        $serial.Close()
      }
    } finally {
      $serial.Dispose()
    }
  }
}
