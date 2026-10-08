param([Parameter(Mandatory)][string]$BuildDirectory, [string]$CompilerDirectory)

$ErrorActionPreference = "Stop"
if (-not $CompilerDirectory) {
  $configText = & arduino-cli config dump --format json
  if ($LASTEXITCODE -ne 0) { throw "Could not locate Arduino compiler for link verification." }
  $config = $configText | ConvertFrom-Json
  $CompilerDirectory = Join-Path $config.config.directories.data "packages/AZ3166/tools/arm-none-eabi-gcc/5_4-2016q3/bin"
}
$suffix = if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) { ".exe" } else { "" }
$nm = Join-Path $CompilerDirectory ("arm-none-eabi-nm" + $suffix)
$objdump = Join-Path $CompilerDirectory ("arm-none-eabi-objdump" + $suffix)
$elf = Join-Path $BuildDirectory "MXCHIPTest1.ino.elf"
$symbols = & $nm -C $elf
if ($LASTEXITCODE -ne 0) { throw "Could not inspect firmware symbols." }
$symbolText = $symbols -join "`n"
foreach ($name in @("EEPROMInterface::read(", "EEPROMInterface::write(", "EEPROMInterface::enableHostSecureChannel(")) {
  if (-not $symbolText.Contains($name)) { throw "Original core EEPROM entry point missing: $name" }
}
$lines = & $objdump --dwarf=decodedline $elf
if ($LASTEXITCODE -ne 0) { throw "Could not inspect firmware source ownership." }
$sourceText = $lines -join "`n"
if ($sourceText.Contains("EEPROMInterfaceCompat.cpp") -or $symbolText -match 'LegacySecureStorage::(read|write|ordinaryAccessAllowed)\(') {
  throw "Unexpected project EEPROM replacement or ordinary-access interception in the firmware."
}
if (-not $sourceText.Contains("EEPROMInterface.cpp")) { throw "Original core EEPROM implementation is not present in the firmware debug information." }
Write-Output "PASS: original core EEPROM implementation is linked, with no project replacement or ordinary-access interception."
