#requires -Version 7.2
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root "src"
$tests = Join-Path $root "tests"
$output = Join-Path $root "_build"
$middleware = Join-Path $root "libraries\STSELib\src"
New-Item -ItemType Directory -Path $output -Force | Out-Null

$setup = Join-Path $output "supplied_key_setup_test.exe"
& g++ -std=c++11 -Wall -Wextra -Werror -pedantic -I (Join-Path $tests "stubs") -I $source -isystem $middleware `
  -include (Join-Path $tests "stubs\ProvisioningHardware.h") (Join-Path $tests "supplied_key_setup_test.cpp") (Join-Path $source "SuppliedKeySetup.cpp") -o $setup
if ($LASTEXITCODE -ne 0) { throw "Supplied-key backend test compilation failed." }
& $setup
if ($LASTEXITCODE -ne 0) { throw "Supplied-key backend tests failed." }

foreach ($mode in @(0,15)) {
  $protocol = Join-Path $output "secure_provisioning_mode_test-$mode.exe"
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $tests "stubs") -I $source `
    (Join-Path $tests "secure_provisioning_mode_test.cpp") (Join-Path $source "SecureProvisioningMode.cpp") -o $protocol
  if ($LASTEXITCODE -ne 0) { throw "Supplied-key protocol test compilation failed." }
  & $protocol
  if ($LASTEXITCODE -ne 0) { throw "Supplied-key protocol tests failed." }
}
& (Join-Path $PSScriptRoot "test-supplied-key-host.ps1")
