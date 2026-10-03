$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$temporaryPath = Join-Path ([System.IO.Path]::GetTempPath()) ("mxchip-package-test-" + [guid]::NewGuid().ToString("N"))
$fixture = (New-Item -ItemType Directory -Path $temporaryPath).FullName
$toolsPath = Join-Path $fixture "tools"
$buildPath = Join-Path $fixture "build"
$firmwarePath = Join-Path $fixture "firmware"
New-Item -ItemType Directory -Path $toolsPath, $buildPath, $firmwarePath | Out-Null
$packager = Join-Path $toolsPath "package-firmware.ps1"
$applicationPath = Join-Path $buildPath "MXCHIPTest1.ino.bin"
$outputPath = Join-Path $buildPath "MXCHIPTest1.full.bin"
$factoryPath = Join-Path $firmwarePath "devkit-firmware-2.0.0.bin"

function Assert-Rejected([string]$name, [string]$message) {
  $rejected = $false
  try {
    & $packager | Out-Null
  } catch {
    if ($_.Exception.Message -ne $message) {
      throw
    }
    $rejected = $true
  }
  if (-not $rejected -or (Test-Path -LiteralPath $outputPath)) {
    throw "Invalid firmware was accepted: $name"
  }
  Write-Output "PASS: $name rejected."
}

try {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "package-firmware.ps1") -Destination $packager
  Copy-Item -LiteralPath (Join-Path (Join-Path $projectRoot "firmware") "devkit-firmware-2.0.0.bin") -Destination $factoryPath
  $factory = [System.IO.File]::ReadAllBytes($factoryPath)
  $header = [byte[]]::new(16)
  [Array]::Copy([BitConverter]::GetBytes([uint32]0x20040000), 0, $header, 0, 4)
  [Array]::Copy([BitConverter]::GetBytes([uint32]0x0800C009), 0, $header, 4, 4)
  [System.IO.File]::WriteAllBytes($applicationPath, $header)

  $invalidFactory = [byte[]]$factory.Clone()
  $invalidFactory[0] = $invalidFactory[0] -bxor 1
  [System.IO.File]::WriteAllBytes($factoryPath, $invalidFactory)
  Assert-Rejected "Changed factory firmware" "The reference firmware checksum does not match the verified DevKit 2.0.0 image."
  [System.IO.File]::WriteAllBytes($factoryPath, $factory)

  [System.IO.File]::WriteAllBytes($applicationPath, [byte[]]::new(7))
  Assert-Rejected "Truncated vector table" "The application image is missing its vector table."
  foreach ($stack in @([uint32]0x10000000, [uint32]0x20000000, [uint32]0x20040008, [uint32]0x2003FFFF)) {
    $invalid = [byte[]]$header.Clone()
    [Array]::Copy([BitConverter]::GetBytes($stack), 0, $invalid, 0, 4)
    [System.IO.File]::WriteAllBytes($applicationPath, $invalid)
    Assert-Rejected "Invalid stack pointer $stack" "The application stack pointer is outside the expected aligned AZ3166 RAM range."
  }
  $invalid = [byte[]]$header.Clone()
  [Array]::Copy([BitConverter]::GetBytes([uint32]0x0800C008), 0, $invalid, 4, 4)
  [System.IO.File]::WriteAllBytes($applicationPath, $invalid)
  Assert-Rejected "Non-Thumb reset vector" "The application reset vector must select Thumb mode."
  foreach ($reset in @([uint32]0x08000035, [uint32]0x0800C011)) {
    $invalid = [byte[]]$header.Clone()
    [Array]::Copy([BitConverter]::GetBytes($reset), 0, $invalid, 4, 4)
    [System.IO.File]::WriteAllBytes($applicationPath, $invalid)
    Assert-Rejected "Out-of-range reset vector $reset" "The application reset vector is outside the expected application flash region."
  }
  [System.IO.File]::WriteAllBytes($applicationPath, [byte[]]::new(0x100000 - 0xC000 + 1))
  Assert-Rejected "One byte beyond flash capacity" "The combined image exceeds the AZ3166's 1 MiB flash."

  $maximum = [byte[]]::new(0x100000 - 0xC000)
  [Array]::Copy($header, 0, $maximum, 0, $header.Length)
  for ($i = $header.Length; $i -lt $maximum.Length; $i++) {
    $maximum[$i] = $i % 251
  }
  [System.IO.File]::WriteAllBytes($applicationPath, $maximum)
  & $packager
  $combined = [System.IO.File]::ReadAllBytes($outputPath)
  if ($combined.Length -ne 0x100000) {
    throw "The maximum-size firmware image has the wrong length."
  }
  for ($i = 0; $i -lt 0xC000; $i++) {
    if ($combined[$i] -ne $factory[$i]) {
      throw "The factory bootloader or padding changed at byte $i."
    }
  }
  for ($i = 0; $i -lt $maximum.Length; $i++) {
    if ($combined[0xC000 + $i] -ne $maximum[$i]) {
      throw "The application payload changed at byte $i."
    }
  }
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $factoryPath).Hash -ne "33A01378A40484F306777DE1E11462918CA9901D1039D2E97AB55D60CC684300") {
    throw "The packager modified the reference firmware."
  }
  Write-Output "PASS: exact flash boundary, unchanged factory prefix, and unchanged application payload."
} finally {
  Remove-Item -LiteralPath $fixture -Recurse -Force
}
