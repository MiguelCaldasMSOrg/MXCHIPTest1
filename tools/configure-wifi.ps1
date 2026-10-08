#requires -Version 7.2
param(
  [string]$Port = "COM8",
  [string]$Ssid,
  [switch]$NoReboot
)

$ErrorActionPreference = "Stop"

function ConvertTo-ConsoleArgument {
  param([Parameter(Mandatory)][AllowEmptyString()][string]$Value)
  '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"'
}

function Read-Until {
  param(
    [Parameter(Mandatory)]$SerialPort,
    [Parameter(Mandatory)][string[]]$Expected,
    [int]$TimeoutSeconds = 8
  )

  $text = ""
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    $text += $SerialPort.ReadExisting()
    if ($text.Length -gt 8192) { throw "Too much unexpected configuration-console output; response suppressed to avoid exposing credentials." }
    foreach ($value in $Expected) {
      if ($text.Contains($value)) {
        return $text
      }
    }
    Start-Sleep -Milliseconds 50
  }
  throw "Timed out waiting for the configuration console. Response suppressed to avoid exposing credentials; settings may be partially updated."
}

if (-not $Ssid) {
  $Ssid = Read-Host "Wi-Fi SSID"
}
# The original console counts the terminating NUL against its 32/64-byte limits.
if ($Ssid.Length -lt 1 -or $Ssid.Length -gt 31 -or $Ssid -match '[^\x20-\x7e]') {
  throw "The SDK console accepts 1-31 printable ASCII SSID characters. Use mode 8 for a 32-character SSID."
}

$securePassword = Read-Host "Wi-Fi password (leave empty for an open network)" -AsSecureString
$passwordPointer = [IntPtr]::Zero
$serialPort = $null
try {
  $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
  $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
  if ($password.Length -gt 63 -or $password -match '[^\x20-\x7e]') {
    throw "The SDK console accepts at most 63 printable ASCII password characters. Use mode 8 for a 64-character key."
  }

  Write-Host "Hold Button A, press Reset, and release A when the onboard display shows Configuration."
  Read-Host "Press Enter when configuration mode is ready"

  $serialPort = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
  $serialPort.NewLine = "`r`n"
  $serialPort.WriteTimeout = 3000
  $serialPort.Open()
  $serialPort.WriteLine("help")
  Read-Until -SerialPort $serialPort -Expected @("Configuration console:") | Out-Null

  $serialPort.WriteLine("set_wifissid $(ConvertTo-ConsoleArgument $Ssid)")
  $response = Read-Until -SerialPort $serialPort -Expected @("INFO: Set Wi-Fi SSID successfully.", "ERROR:", "Invalid")
  if (-not $response.Contains("INFO: Set Wi-Fi SSID successfully.")) {
    throw "SSID save was not verified. Console output suppressed; settings may be partially updated."
  }

  $serialPort.WriteLine("set_wifipwd $(ConvertTo-ConsoleArgument $password)")
  $response = Read-Until -SerialPort $serialPort -Expected @("INFO: Set Wi-Fi password successfully.", "ERROR:", "Invalid")
  if (-not $response.Contains("INFO: Set Wi-Fi password successfully.")) {
    throw "Password save was not verified. Console output suppressed; settings may be partially updated."
  }

  Write-Host "Wi-Fi credentials saved and verified in STSAFE EEPROM."
  if (-not $NoReboot) {
    $serialPort.WriteLine("exit")
    Write-Host "Board reboot requested."
  }
} finally {
  if ($passwordPointer -ne [IntPtr]::Zero) {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
  }
  $password = $null
  $response = $null
  $securePassword.Dispose()
  if ($serialPort) {
    try {
      if ($serialPort.IsOpen) {
        $serialPort.Close()
      }
    } finally {
      $serialPort.Dispose()
    }
  }
}
