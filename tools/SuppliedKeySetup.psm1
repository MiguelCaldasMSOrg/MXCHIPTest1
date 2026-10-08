#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-SuppliedHostKeys {
  param([Parameter(Mandatory)][byte[]]$Keys)
  if ($Keys.Length -ne 32) { throw "Supply exactly 32 bytes: MAC key followed by cipher key." }
  if ([Convert]::ToHexString($Keys,0,16) -ceq [Convert]::ToHexString($Keys,16,16)) { throw "MAC and cipher keys must differ." }
  foreach ($offset in @(0,16)) {
    $part = @($Keys[$offset..($offset+15)])
    if (@($part | Where-Object { $_ -ne 0 }).Count -eq 0 -or @($part | Where-Object { $_ -ne 255 }).Count -eq 0) {
      throw "All-zero or all-FF host keys are not accepted."
    }
  }
}

function Connect-SuppliedKeyTarget {
  param([Parameter(Mandatory)][string]$Port)
  $serial = [IO.Ports.SerialPort]::new($Port,115200,'None',8,'One')
  $serial.ReadTimeout = 1000
  $serial.WriteTimeout = 3000
  $serial.NewLine = "`r`n"
  try {
    $serial.Open()
    $serial.DiscardInBuffer()
    Write-Host "Press Reset without holding A or B. Waiting for supplied-key mode 15; no commands are sent to an unidentified sketch."
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    while ([DateTime]::UtcNow -lt $deadline) {
      try {
        if ($serial.ReadLine().Trim() -ceq "MXCHIP_SUPPLIED_KEYS_MODE15_V1") { return $serial }
      } catch [TimeoutException] {}
    }
    throw "The supplied-key mode-15 banner was not received. No setup command was sent."
  } catch {
    $serial.Dispose()
    throw
  }
}

function Invoke-SuppliedKeyCommand {
  param(
    [Parameter(Mandatory)]$Serial,
    [Parameter(Mandatory)][string]$Command,
    [Parameter(Mandatory)][string]$Expected,
    [int]$TimeoutSeconds = 30
  )
  if ($Command.Length -gt 126 -or $Command -match '[^\x20-\x7E]') { throw "Invalid local command." }
  $Serial.WriteLine($Command)
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  $received = 0
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $Serial.ReadLine().Trim()
      $received += $line.Length
      if ($received -gt 8192) { throw "Too much unexpected serial output." }
      if ($line -cmatch '^SK2 ERR ([A-Z0-9_]+)$') { throw "Device error: $($Matches[1]). Setup may be partial; this tool provides no undo or repair." }
      if ($line -cmatch $Expected) { return $line }
    } catch [TimeoutException] {}
  }
  throw "Device response timed out. Setup may be partial; no automatic retry or repair was attempted."
}

function Get-SuppliedKeyStatus {
  param([Parameter(Mandatory)]$Serial)
  $pattern = '^SK2 STATUS ([0-9A-F]{24}) ([0-2]) ([01]) ([01]) ([01]) ([01])$'
  $line = Invoke-SuppliedKeyCommand $Serial "SK2 STATUS" $pattern
  $null = $line -cmatch $pattern
  [pscustomobject]@{
    Uid = $Matches[1]
    Rdp = [int]$Matches[2]
    Pcrop = $Matches[3] -eq "1"
    HostKeysPresent = $Matches[4] -eq "1"
    EnvelopeKeyPresent = $Matches[5] -eq "1"
    HostFlashEmpty = $Matches[6] -eq "1"
  }
}

Export-ModuleMember -Function Assert-SuppliedHostKeys, Connect-SuppliedKeyTarget, Invoke-SuppliedKeyCommand, Get-SuppliedKeyStatus
