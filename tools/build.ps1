$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $projectRoot "build"
$buildSketchPath = Join-Path $projectRoot "_build\MXCHIPTest1"
$databasePath = Join-Path $buildPath "compile_commands.json"
$sketchPath = Join-Path $projectRoot "MXCHIPTest1.ino"

Push-Location $projectRoot
try {
  # Match the release build without inheriting local upload-port metadata.
  New-Item -ItemType Directory -Path $buildSketchPath -Force | Out-Null
  Copy-Item -LiteralPath $sketchPath -Destination $buildSketchPath -Force
  & arduino-cli compile --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 --build-path $buildPath --output-dir $buildPath $buildSketchPath
  if ($LASTEXITCODE -ne 0) {
    throw "Arduino compilation failed with exit code $LASTEXITCODE."
  }

  & (Join-Path $PSScriptRoot "package-firmware.ps1")

  $database = @(Get-Content $databasePath -Raw | ConvertFrom-Json)
  $generatedEntry = $database | Where-Object { $_.file -like "*MXCHIPTest1.ino.cpp" } | Select-Object -First 1
  if (-not $generatedEntry) {
    throw "The compilation database does not contain the generated sketch entry."
  }

  $generatedPath = $generatedEntry.file
  $sketchEntry = $generatedEntry | ConvertTo-Json -Depth 20 | ConvertFrom-Json
  $sketchEntry.file = $sketchPath
  $sketchEntry.arguments = @($sketchEntry.arguments | ForEach-Object {
    if ($_ -eq $generatedPath) {
      "-include"
      "Arduino.h"
      $sketchPath
    } else {
      $_
    }
  })

  @($database) + $sketchEntry |
    ConvertTo-Json -Depth 20 |
    Set-Content $databasePath -Encoding utf8
} finally {
  Pop-Location
}
