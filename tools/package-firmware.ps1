$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $projectRoot "build"
$applicationPath = Join-Path $buildPath "MXCHIPTest1.ino.bin"
$factoryPath = Join-Path (Join-Path $projectRoot "firmware") "devkit-firmware-2.0.0.bin"
$outputPath = Join-Path $buildPath "MXCHIPTest1.full.bin"
$applicationOffset = 0xC000
$applicationAddress = 0x08000000 + $applicationOffset
$flashSize = 0x100000

if ((Get-FileHash -Algorithm SHA256 -LiteralPath $factoryPath).Hash -ne "33A01378A40484F306777DE1E11462918CA9901D1039D2E97AB55D60CC684300") {
  throw "The reference firmware checksum does not match the verified DevKit 2.0.0 image."
}

$factory = [System.IO.File]::ReadAllBytes($factoryPath)
$application = [System.IO.File]::ReadAllBytes($applicationPath)
if ($application.Length -lt 8) {
  throw "The application image is missing its vector table."
}
if ($applicationOffset + $application.Length -gt $flashSize) {
  throw "The combined image exceeds the AZ3166's 1 MiB flash."
}

$stackPointer = [BitConverter]::ToUInt32($application, 0)
$resetVector = [BitConverter]::ToUInt32($application, 4)
if ($stackPointer -lt 0x20000000 -or $stackPointer -gt 0x20040000 -or $stackPointer % 8 -ne 0) {
  throw "The application stack pointer is outside the expected aligned AZ3166 RAM range."
}
if ($resetVector % 2 -ne 1) {
  throw "The application reset vector must select Thumb mode."
}
$resetAddress = [long]$resetVector - 1
if ($resetAddress -lt $applicationAddress -or $resetAddress -ge $applicationAddress + $application.Length) {
  throw "The application reset vector is outside the expected application flash region."
}

$image = [byte[]]::new($applicationOffset + $application.Length)
[Array]::Copy($factory, 0, $image, 0, $applicationOffset)
[Array]::Copy($application, 0, $image, $applicationOffset, $application.Length)
[System.IO.File]::WriteAllBytes($outputPath, $image)

Write-Output "Created $outputPath ($($image.Length) bytes; flash base 0x08000000)."
