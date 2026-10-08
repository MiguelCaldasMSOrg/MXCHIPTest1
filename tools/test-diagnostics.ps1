$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $projectRoot "src"
$testDirectory = Join-Path $projectRoot "tests"
$outputDirectory = Join-Path $projectRoot "_build"
$middlewareDirectory = Join-Path (Join-Path (Join-Path $projectRoot "libraries") "STSELib") "src"
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$cases = @(
  @{ Name = "onboard_dispatch"; Modes = @(0..15); Sources = @("OnboardTests.cpp") },
  @{ Name = "diagnostic_checks"; Modes = @(0); Sources = @() },
  @{ Name = "wifi"; Modes = @(0, 5); Sources = @("WiFiTests.cpp") },
  @{ Name = "wifi_provisioning"; Modes = @(0, 8); Sources = @("WiFiProvisioning.cpp") },
  @{ Name = "grove_oled"; Modes = @(0, 6); Sources = @("GroveOledTests.cpp") },
  @{ Name = "sensor"; Modes = @(0, 9); Sources = @("SensorTests.cpp") },
  @{ Name = "microphone"; Modes = @(0, 10); Sources = @("MicrophoneTests.cpp") },
  @{ Name = "filesystem"; Modes = @(0, 11); Sources = @("FileSystemTests.cpp") },
  @{ Name = "network"; Modes = @(0, 12); Sources = @("NetworkTests.cpp") },
  @{ Name = "peripheral_diagnostics"; Modes = @(0, 13, 14); Sources = @("IrdaTests.cpp", "SecurityChipTests.cpp") }
)
foreach ($case in $cases) {
  $sources = @((Join-Path $testDirectory "$($case.Name)_test.cpp"))
  $sources += @($case.Sources | ForEach-Object { Join-Path $sourceDirectory $_ })
  foreach ($mode in $case.Modes) {
    $executable = Join-Path $outputDirectory "$($case.Name)_test-$mode.exe"
    & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $testDirectory "stubs") -I $sourceDirectory -isystem $middlewareDirectory @sources -o $executable
    if ($LASTEXITCODE -ne 0) {
      throw "$($case.Name) compilation failed in mode $mode (exit $LASTEXITCODE)."
    }
    & $executable
    if ($LASTEXITCODE -ne 0) {
      throw "$($case.Name) failed in mode $mode (exit $LASTEXITCODE)."
    }
  }
}

& (Join-Path $PSScriptRoot "test-wifi-console.ps1")

$config = Get-Content -LiteralPath (Join-Path $sourceDirectory "NetworkTestConfig.h") -Raw
$pem = [regex]::Match($config, '-----BEGIN CERTIFICATE-----[\s\S]+?-----END CERTIFICATE-----').Value
$der = [Convert]::FromBase64String(($pem -replace '-----(BEGIN|END) CERTIFICATE-----|\s', ''))
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($der)
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
  $hash = [BitConverter]::ToString($sha256.ComputeHash($certificate.RawData)).Replace("-", "")
  if ($hash -ne "96BCEC06264976F37460779ACF28C5A7CFE8A3C0AAE11A8FFCEE05C0BDDF08C6") {
    throw "ISRG Root X1 trust-anchor fingerprint mismatch."
  }
  Write-Output "PASS: embedded ISRG Root X1 certificate parses and matches the published SHA-256 fingerprint."
} finally {
  $sha256.Dispose()
  $certificate.Dispose()
}
