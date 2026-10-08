$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$temporaryPath = Join-Path ([System.IO.Path]::GetTempPath()) ("mxchip-validate-test-" + [guid]::NewGuid().ToString("N"))
$fixture = (New-Item -ItemType Directory -Path $temporaryPath).FullName
$toolsPath = Join-Path $fixture "tools"
$buildPath = Join-Path $fixture "build"
New-Item -ItemType Directory -Path $toolsPath, $buildPath | Out-Null
$validator = Join-Path $toolsPath "validate-firmware.ps1"
$applicationPath = Join-Path $buildPath "MXCHIPTest1.ino.bin"
$outputPath = Join-Path $buildPath "MXCHIPTest1.full.bin"

function Assert-Rejected([string]$name, [string]$message) {
  $rejected = $false
  try {
    & $validator | Out-Null
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
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "validate-firmware.ps1") -Destination $validator
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -Destination $toolsPath
  $header = [byte[]]::new(16)
  [Array]::Copy([BitConverter]::GetBytes([uint32]0x20040000), 0, $header, 0, 4)
  [Array]::Copy([BitConverter]::GetBytes([uint32]0x0800C009), 0, $header, 4, 4)
  [System.IO.File]::WriteAllBytes($applicationPath, $header)

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
  Assert-Rejected "One byte beyond flash capacity" "The application exceeds the AZ3166's flash capacity after offset 0xC000."

  $maximum = [byte[]]::new(0x100000 - 0xC000)
  [Array]::Copy($header, 0, $maximum, 0, $header.Length)
  for ($i = $header.Length; $i -lt $maximum.Length; $i++) {
    $maximum[$i] = $i % 251
  }
  [System.IO.File]::WriteAllBytes($applicationPath, $maximum)
  $beforeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $applicationPath).Hash
  & $validator
  if (Test-Path -LiteralPath $outputPath) {
    throw "Application validation unexpectedly created a full image."
  }
  $validated = [System.IO.File]::ReadAllBytes($applicationPath)
  for ($i = 0; $i -lt $maximum.Length; $i++) {
    if ($validated[$i] -ne $maximum[$i]) {
      throw "The application payload changed at byte $i."
    }
  }
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $applicationPath).Hash -ne $beforeHash) {
    throw "Validation modified the application."
  }
  Write-Output "PASS: exact application flash boundary, unchanged payload, no factory-image dependency, and no full image produced."
} finally {
  Remove-Item -LiteralPath $fixture -Recurse -Force
}
