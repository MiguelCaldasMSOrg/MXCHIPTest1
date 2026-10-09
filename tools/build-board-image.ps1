#requires -Version 7.2
<#
.SYNOPSIS
Creates a confidential, same-board STM32 image from two live prefix reads and an already-built application.
.DESCRIPTION
Never erases, programs, unlocks, or resets flash. Temporarily halts a running STM32
and restores its prior state. Requires a fixed local NTFS/ReFS output outside the
repository. STSAFE contents, non-exportable keys and QSPI are NOT backed up.
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = "High")]
param(
  [string]$Application,
  [string]$OutputDirectory,
  [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$SerialNumber,
  [string]$OpenOcdRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "BoardToolOutput.ps1")
Import-Module (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -DisableNameChecking -Force
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Application) { $Application = Join-Path $projectRoot "build\MXCHIPTest1.ino.bin" }
$applicationPath = (Resolve-Path -LiteralPath $Application).Path
$applicationBytes = [IO.File]::ReadAllBytes($applicationPath)
Assert-MxImage -Bytes $applicationBytes
$applicationHash = Get-MxHash $applicationBytes
$probe = Get-MxStLink -SerialNumber $SerialNumber
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $env:LOCALAPPDATA ("MXCHIPTest1\BoardImages\{0}-{1}-{2}" -f $probe.SerialNumber, [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ"), [Guid]::NewGuid().ToString("N").Substring(0,8))
}
$output = Assert-MxLocalDirectory -Path $OutputDirectory -ForbiddenRoot $projectRoot
if (Test-Path -LiteralPath $output) { throw "Output directory already exists; nothing will be overwritten." }
$openocd = Resolve-MxOpenOcd -Root $OpenOcdRoot -UsbPid $probe.UsbPid
Write-Verbose "Selected OpenOCD: $($openocd.Exe); $($openocd.Version)"
$snapshot = New-MxSnapshotConfiguration -SerialNumber $probe.SerialNumber -UsbPid $probe.UsbPid -InterfaceScript $openocd.InterfaceScript
Write-Output "Board: $($probe.SerialNumber); application SHA-256: $applicationHash"
Write-Output "Output: $output"
Write-Warning "The resulting image may contain plaintext host secrets. It is for THIS board only, not a STSAFE backup. No flash writes will occur, but the CPU pauses during snapshot."
if (-not $PSCmdlet.ShouldProcess($probe.SerialNumber, "Read STM32 prefix twice and create a confidential same-board image")) { return }

New-MxPrivateDirectory $output
$completed = $false
$image = $null
$prefix = $null
$second = $null
$operation = [pscustomobject]@{
  LogPath = Join-Path $output "operation.log"
  ShowDiagnostics = $VerbosePreference -eq "Continue"
  Stage = "Snapshot"
  LastState = "Initial run/halt state not yet recorded"
}
try {
  [IO.File]::WriteAllText($operation.LogPath, "Confidential same-board snapshot diagnostics; may contain device identities and local paths.`r`n")
  Write-Output "Diagnostics: $($operation.LogPath)"
  Write-Host "Reading and checking the same-board image..."
  $config = Join-Path $output "snapshot.cfg"
  [IO.File]::WriteAllText($config, $snapshot, [Text.UTF8Encoding]::new($false))
  try {
    $result = Invoke-MxTool $openocd.Exe @("-s", $openocd.Scripts, "-f", $config) $output -TimeoutSeconds 90
    Write-MxMscDiagnostic $operation "Snapshot exit $($result.ExitCode):`r`n$($result.Text)"
    if ($result.ExitCode -ne 0 -or $result.Text -notmatch '(?m)^MXCHIP_SNAPSHOT_OK\s*$') {
      throw "The snapshot or target-state restoration was not confirmed. No image will be published."
    }
    $operation.LastState = "Original CPU run/halt and watchdog-debug state restored"
  } catch {
    $originalError = $_
    $statePath = Join-Path $output "state.txt"
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
      try {
        $operation.Stage = "Restoring target execution state"
        Write-Host "Restoring the board's execution state..."
        $recovery = New-MxSnapshotRecoveryConfiguration $probe.SerialNumber $probe.UsbPid ([IO.File]::ReadAllText($statePath)) -InterfaceScript $openocd.InterfaceScript
        $recoveryPath = Join-Path $output "recover.cfg"
        [IO.File]::WriteAllText($recoveryPath, $recovery, [Text.UTF8Encoding]::new($false))
        $restored = Invoke-MxTool $openocd.Exe @("-s", $openocd.Scripts, "-f", $recoveryPath) $output -TimeoutSeconds 20
        Write-MxMscDiagnostic $operation "State restoration exit $($restored.ExitCode):`r`n$($restored.Text)"
        if ($restored.ExitCode -ne 0 -or $restored.Text -notmatch '(?m)^MXCHIP_RECOVERY_OK\s*$') { throw "Target-state restoration was not confirmed." }
        $operation.LastState = "Recorded execution/debug state restored"
        $operation.Stage = "Snapshot"
      } catch {
        throw "Snapshot failed and execution-state recovery failed: $($_.Exception.Message). The board may still be halted; inspect it before continuing."
      }
      $operation.Stage = "Verifying image"
    }
    throw $originalError
  }
  $prefix = [IO.File]::ReadAllBytes((Join-Path $output "prefix-a.bin"))
  $second = [IO.File]::ReadAllBytes((Join-Path $output "prefix-b.bin"))
  $uidBytes = [IO.File]::ReadAllBytes((Join-Path $output "target-uid.bin"))
  if ($uidBytes.Length -ne 12 -or @($uidBytes | Where-Object { $_ -ne 0 }).Count -eq 0 -or @($uidBytes | Where-Object { $_ -ne 255 }).Count -eq 0) {
    throw "Invalid STM32 unique ID read."
  }
  $image = Join-MxBoardImage -Prefix $prefix -SecondRead $second -Application $applicationBytes
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $applicationPath).Hash -ne $applicationHash) {
    throw "Application changed during snapshot; build again before making a board image."
  }
  $uid = [Convert]::ToHexString($uidBytes)
  $imagePath = Join-Path $output "MXCHIPTest1.$uid.board.full.bin"
  [IO.File]::WriteAllBytes($imagePath, $image)
  $imageHash = Get-MxHash $image
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $imagePath).Hash -ne $imageHash) { throw "Written image verification failed." }
  $manifest = [ordered]@{
    format = "MXCHIP-same-board-image-v1"
    createdUtc = [DateTime]::UtcNow.ToString("o")
    confidential = $true
    stLinkSerial = $probe.SerialNumber
    stm32UniqueIdBytes = $uid
    stm32DeviceId = "0x441"
    flashBase = "0x08000000"
    prefixLength = 0xC000
    applicationAddress = "0x0800C000"
    imageLength = $image.Length
    prefixSha256 = Get-MxHash $prefix
    applicationSha256 = $applicationHash
    imageSha256 = $imageHash
    imageFile = [IO.Path]::GetFileName($imagePath)
    openOcdVersion = $openocd.Version
    excludes = @("STSAFE data and keys", "External QSPI", "STM32 option bytes and OTP", "STM32 flash after the new application")
    warning = "Secret-bearing same-board image, not a complete device backup. Never commit, publish, or use on another board."
  }
  [IO.File]::WriteAllText((Join-Path $output "manifest.json"), ($manifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
  Write-Output "SHA-256: $imageHash"
  $completed = $true
} catch {
  $reason = $_.Exception.Message
  try { Write-MxMscDiagnostic $operation "TERMINAL ERROR: $reason" } catch { $reason += " Diagnostic logging also failed: $($_.Exception.Message)" }
  throw "Board-image creation failed.`nStage: $($operation.Stage)`nLast state: $($operation.LastState)`nReason: $reason`nDiagnostics: $($operation.LogPath)"
} finally {
  foreach ($bytes in @($image, $prefix, $second, $applicationBytes)) {
    if ($null -ne $bytes) { [Array]::Clear($bytes, 0, $bytes.Length) }
  }
  # Delete only files generated in this newly created private directory.
  foreach ($name in @("prefix-a.bin", "prefix-b.bin", "target-uid.bin", "snapshot.cfg", "recover.cfg", "state.txt")) {
    $path = Join-Path $output $name
    if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
  }
  if (-not $completed) {
    foreach ($path in @((Join-Path $output "manifest.json"), (Get-Variable imagePath -ValueOnly -ErrorAction SilentlyContinue))) {
      if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) { Remove-Item -LiteralPath $path -Force }
    }
  }
}
Write-BoardVerified "same-board image saved to $imagePath; original CPU state restored. No flash or STSAFE writes."
