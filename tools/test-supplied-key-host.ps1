#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$module = Import-Module (Join-Path $PSScriptRoot "SuppliedKeySetup.psm1") -Force -PassThru
$entry = Join-Path $PSScriptRoot "provision-stsafe.ps1"

function Assert([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw "FAIL: $Message" }
}

function Reject([scriptblock]$Operation, [string]$Message, [string]$ExpectedError = "") {
  $errorRecord = $null
  try { & $Operation | Out-Null } catch { $errorRecord = $_ }
  Assert ($null -ne $errorRecord) $Message
  if ($ExpectedError) {
    Assert ($errorRecord.Exception.Message -match $ExpectedError) "$Message (unexpected error: $($errorRecord.Exception.Message))"
  }
}

class SuppliedKeySerialFixture {
  [Collections.Generic.List[string]]$Commands = [Collections.Generic.List[string]]::new()
  [Collections.Generic.Queue[string]]$Lines = [Collections.Generic.Queue[string]]::new()
  [string]$Uid = "0102030405060708090A0B0C"
  [int]$Rdp = 0
  [bool]$Pcrop = $false
  [bool]$HostKeysPresent = $false
  [bool]$EnvelopeKeyPresent = $false
  [bool]$HostFlashEmpty = $true
  [bool]$AutomaticResponses = $true
  [bool]$BadAfterStatus = $false
  [string]$SetError = ""
  [bool]$FailClose = $false
  [bool]$Disposed = $false
  [bool]$IsOpen = $true
  [int]$SetCount = 0

  [void]WriteLine([string]$Command) {
    $this.Commands.Add($Command)
    if (-not $this.AutomaticResponses) { return }
    if ($Command -ceq "SK2 STATUS") {
      $this.Lines.Enqueue(("SK2 STATUS {0} {1} {2} {3} {4} {5}" -f $this.Uid, $this.Rdp, [int]$this.Pcrop, [int]$this.HostKeysPresent, [int]$this.EnvelopeKeyPresent, [int]$this.HostFlashEmpty))
    } elseif ($Command -ceq "SK2 ABORT") {
      $this.Lines.Enqueue("SK2 ABORTED")
    } elseif ($Command -cmatch '^SK2 SET [0-9A-F]{24} [0-9A-Fa-f]{64}$') {
      $this.SetCount++
      if ($this.SetError) {
        $this.Lines.Enqueue("SK2 ERR " + $this.SetError)
      } else {
        $this.HostKeysPresent = $true
        $this.EnvelopeKeyPresent = -not $this.BadAfterStatus
        $this.HostFlashEmpty = $false
        $this.Lines.Enqueue("SK2 WORKING")
        $this.Lines.Enqueue("SK2 INSTALLED")
      }
    } else {
      throw "Unexpected fixture command"
    }
  }

  [string]ReadLine() {
    if ($this.Lines.Count -eq 0) { throw [TimeoutException]::new("Fixture read timeout") }
    return $this.Lines.Dequeue()
  }

  [void]Close() {
    if ($this.FailClose) { throw [IO.IOException]::new("Fixture close failure") }
    $this.IsOpen = $false
  }

  [void]Dispose() { $this.Disposed = $true }
}

$publicKeys = [byte[]](1..32)
Assert-SuppliedHostKeys $publicKeys
foreach ($size in @(1,16,31,33,64)) {
  Reject { Assert-SuppliedHostKeys ([byte[]]::new($size)) } "fixed MAC-then-cipher input length"
}
foreach ($offset in @(0,16)) {
  foreach ($fill in @(0,255)) {
    $invalidKeys = [byte[]]$publicKeys.Clone()
    for ($index = $offset; $index -lt $offset+16; $index++) { $invalidKeys[$index] = $fill }
    Reject { Assert-SuppliedHostKeys $invalidKeys } "zero/FF keys rejected"
  }
}
$equalKeys = [byte[]]($publicKeys[0..15] + $publicKeys[0..15])
Reject { Assert-SuppliedHostKeys $equalKeys } "identical MAC and cipher keys rejected"

$serial = [SuppliedKeySerialFixture]::new()
$status = Get-SuppliedKeyStatus $serial
Assert ($status.Uid -ceq $serial.Uid -and $status.Rdp -eq 0 -and -not $status.Pcrop -and -not $status.HostKeysPresent -and -not $status.EnvelopeKeyPresent -and $status.HostFlashEmpty) "status wire shape"
$serial = [SuppliedKeySerialFixture]::new()
$serial.AutomaticResponses = $false
$serial.Lines.Enqueue("SK2 STATUS 0102030405060708090A0B0C 0 0 2 0 1")
$serial.Lines.Enqueue("SK2 ERR INVALID_STATUS_FIXTURE")
Reject { Get-SuppliedKeyStatus $serial } "out-of-range status cannot be parsed as success"
$serial.Lines.Enqueue("SK2 ERR HOST_KEY_WRITE")
Reject { Invoke-SuppliedKeyCommand $serial "SK2 STATUS" '^SK2 INSTALLED$' } "explicit device errors propagated"
$serial.Lines.Enqueue(("X" * 8193))
Reject { Invoke-SuppliedKeyCommand $serial "SK2 STATUS" '^SK2 INSTALLED$' } "bounded unexpected output"
$before = $serial.Commands.Count
Reject { Invoke-SuppliedKeyCommand $serial ("X" * 127) '^SK2 INSTALLED$' } "oversize command rejected locally"
Reject { Invoke-SuppliedKeyCommand $serial "SK2 STATUS`nSK2 ABORT" '^SK2 INSTALLED$' } "line injection rejected locally"
Assert ($serial.Commands.Count -eq $before) "invalid local commands never sent"
Reject { Invoke-SuppliedKeyCommand $serial "SK2 STATUS" '^SK2 INSTALLED$' -TimeoutSeconds 0 } "timeout explicitly fails"
Assert ($serial.Commands.Count -eq $before+1) "timeout never resends the command"

$originalConnect = & $module { (Get-Item Function:Connect-SuppliedKeyTarget).ScriptBlock }
$hostInput = [pscustomobject]@{ Prompts = 0; KeyText = ""; Confirmation = "" }
function Import-Module {
  [CmdletBinding()]
  param([Parameter(Position=0, Mandatory)][string]$Name, [switch]$Force)
  if ([IO.Path]::GetFullPath($Name) -ne $module.Path -or -not $Force) { throw "Expected forced import of supplied-key module" }
  Microsoft.PowerShell.Core\Import-Module $Name
}
function Read-Host {
  [CmdletBinding()]
  param([Parameter(Position=0)][string]$Prompt, [switch]$AsSecureString)
  $hostInput.Prompts++
  if ($AsSecureString) { return ConvertTo-SecureString $hostInput.KeyText -AsPlainText -Force }
  if ($Prompt.StartsWith("Type exactly")) { return $hostInput.Confirmation }
  if ($Prompt.StartsWith("Press Button A")) { return "" }
  throw "Unexpected fixture prompt"
}
function New-WorkflowFixture {
  $script:fakeSerial = [SuppliedKeySerialFixture]::new()
  $hostInput.KeyText = [Convert]::ToHexString($publicKeys)
  $hostInput.Confirmation = "SETKEYS $($script:fakeSerial.Uid)"
  $hostInput.Prompts = 0
  & $module {
    param($Serial)
    $script:fixture = $Serial
    $script:connections = 0
  } $script:fakeSerial
}
& $module {
  function script:Connect-SuppliedKeyTarget {
    param([Parameter(Mandatory)][string]$Port)
    if ($Port -cne "FIXTURE") { throw "Host tests must never open a real serial port" }
    $script:connections++
    return $script:fixture
  }
}

try {
  New-WorkflowFixture
  & $entry -Port FIXTURE -Action ProvisionSupplied -WhatIf 6>$null | Out-Null
  Assert ((& $module { $script:connections }) -eq 0 -and $hostInput.Prompts -eq 0) "WhatIf never connects or reads secrets"
  Reject { & $entry -Port FIXTURE -Action ProvisionSupplied -Confirm:$false } "hardware acknowledgement required" 'AcknowledgeUnvalidatedHardware'
  Assert ((& $module { $script:connections }) -eq 0) "acknowledgement gate before connecting"
  foreach ($action in @("ProvisionRandom","Resume","RestoreHostKeys","RecoveryImage","ArmRdp1","CancelRdp1")) {
    Reject { & $entry -Port FIXTURE -Action $action -Confirm:$false } "removed action rejected: $action" "parameter 'Action'"
  }
  Assert ((& $module { $script:connections }) -eq 0) "removed actions cannot reach a device"

  New-WorkflowFixture
  & $entry -Port FIXTURE -Action Status -Confirm:$false | Out-Null
  Assert ($fakeSerial.SetCount -eq 0 -and $fakeSerial.Disposed -and $hostInput.Prompts -eq 0) "status is read-only and closes the connection"
  Assert (($fakeSerial.Commands -join ',') -ceq "SK2 STATUS,SK2 ABORT") "status and transient disarm only"

  foreach ($reason in @("Rdp","Pcrop","HostKeysPresent","HostFlashEmpty")) {
    New-WorkflowFixture
    switch ($reason) {
      Rdp { $fakeSerial.Rdp = 1 }
      Pcrop { $fakeSerial.Pcrop = $true }
      HostKeysPresent { $fakeSerial.HostKeysPresent = $true }
      HostFlashEmpty { $fakeSerial.HostFlashEmpty = $false }
    }
    Reject { & $entry -Port FIXTURE -Action ProvisionSupplied -AcknowledgeUnvalidatedHardware -Confirm:$false } "precondition rejects $reason" '(Host flash is protected|STSAFE host keys already exist|host-key sector is not empty)'
    Assert ($fakeSerial.SetCount -eq 0 -and $hostInput.Prompts -eq 0 -and $fakeSerial.Disposed) "precondition checked before requesting keys"
  }

  foreach ($badInput in @("00",("G"*64),("0"*64),([Convert]::ToHexString($equalKeys)))) {
    New-WorkflowFixture
    $hostInput.KeyText = $badInput
    Reject { & $entry -Port FIXTURE -Action ProvisionSupplied -AcknowledgeUnvalidatedHardware -Confirm:$false } "invalid supplied input rejected" '(Expected exactly 64|All-zero or all-FF|MAC and cipher keys must differ)'
    Assert ($fakeSerial.SetCount -eq 0 -and $fakeSerial.Disposed) "invalid keys not transmitted"
  }
  New-WorkflowFixture
  $hostInput.Confirmation = "wrong board"
  Reject { & $entry -Port FIXTURE -Action ProvisionSupplied -AcknowledgeUnvalidatedHardware -Confirm:$false 3>$null } "board-specific confirmation required" 'Key setup not confirmed'
  Assert ($fakeSerial.SetCount -eq 0) "confirmation mismatch has no persistent request"

  foreach ($deviceError in @("PHYSICAL_CONFIRMATION_REQUIRED","HOST_KEY_WRITE","HOST_KEYS_VERIFY","HOST_FLASH_WRITE_VERIFY")) {
    New-WorkflowFixture
    $fakeSerial.SetError = $deviceError
    Reject { & $entry -Port FIXTURE -Action ProvisionSupplied -AcknowledgeUnvalidatedHardware -Confirm:$false 3>$null } "device failure propagated: $deviceError" "Device error: $deviceError"
    Assert ($fakeSerial.SetCount -eq 1 -and $fakeSerial.Disposed) "device failure has no retry or restore command"
  }
  New-WorkflowFixture
  $fakeSerial.BadAfterStatus = $true
  Reject { & $entry -Port FIXTURE -Action ProvisionSupplied -AcknowledgeUnvalidatedHardware -Confirm:$false 3>$null } "post-setup state verified" 'did not report the expected key state'
  Assert ($fakeSerial.SetCount -eq 1) "unexpected post-setup state is not repaired"

  New-WorkflowFixture
  $text = (& $entry -Port FIXTURE -Action ProvisionSupplied -AcknowledgeUnvalidatedHardware -Confirm:$false 3>&1 | Out-String)
  Assert ($text.Contains("Supplied keys and legacy STM32 loaders installed") -and -not $text.Contains($hostInput.KeyText)) "success is explicit without echoing keys"
  Assert ($fakeSerial.SetCount -eq 1 -and $fakeSerial.Disposed -and $hostInput.Prompts -eq 3) "one supplied-key operation after both confirmations"
  Assert (($fakeSerial.Commands -join ',') -ceq "SK2 STATUS,SK2 SET $($fakeSerial.Uid) $($hostInput.KeyText),SK2 STATUS,SK2 ABORT") "exact supplied-only command sequence with MAC first"

  New-WorkflowFixture
  $fakeSerial.FailClose = $true
  Reject { & $entry -Port FIXTURE -Action Status -Confirm:$false } "serial close error is explicit" 'Fixture close failure'
  Assert ($fakeSerial.Disposed) "serial is disposed even after a close error"
} finally {
  & $module { param($Original) Set-Item Function:script:Connect-SuppliedKeyTarget -Value $Original } $originalConnect
  Remove-Module $module -Force
}
Write-Output "PASS: supplied-only host actions, exact protocol, input/confirmation gates, WhatIf, explicit failures, no retries/repair and no real serial I/O."
