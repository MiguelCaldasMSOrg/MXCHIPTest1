$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$middleware = Join-Path (Join-Path (Join-Path $projectRoot "libraries") "STSELib") "src"
$tests = Join-Path $projectRoot "tests"
$output = Join-Path (Join-Path $projectRoot "_build") "stsafe-host"
New-Item -ItemType Directory -Path $output -Force | Out-Null
& (Join-Path $PSScriptRoot "verify-stselib.ps1")

$sources = @(
  "api\stse_device_management.c",
  "api\stse_random.c",
  "core\stse_device.c",
  "core\stse_frame.c",
  "core\stse_generic_typedef.c",
  "services\stsafea\stsafea_frame_transfer.c",
  "services\stsafea\stsafea_commands.c",
  "services\stsafea\stsafea_put_query.c",
  "services\stsafea\stsafea_host_key_slot.c",
  "services\stsafea\stsafea_hash.c",
  "services\stsafea\stsafea_low_power.c",
  "services\stsafea\stsafea_password.c",
  "services\stsafea\stsafea_reset.c",
  "services\stsafea\stsafea_echo.c",
  "services\stsafea\stsafea_random.c",
  "services\stsafea\stsafea_sessions.c",
  "services\stsafea\stsafea_timings.c"
)
$objects = @()
foreach ($source in $sources) {
  $path = Join-Path $middleware ($source.Replace('\', [System.IO.Path]::DirectorySeparatorChar))
  $object = Join-Path $output ([System.IO.Path]::GetFileNameWithoutExtension($path) + ".o")
  & gcc -std=gnu99 -O1 -ffunction-sections -fdata-sections -I $middleware -c $path -o $object
  if ($LASTEXITCODE -ne 0) {
    throw "STSELib C compilation failed: $source"
  }
  $objects += $object
}
$executable = Join-Path $output "stsafe_transport_test.exe"
& g++ -std=c++11 -O1 -Wall -Wextra -Werror -pedantic -ffunction-sections -fdata-sections -I (Join-Path $tests "stubs") -isystem $middleware `
  (Join-Path $tests "stsafe_transport_test.cpp") (Join-Path $middleware "az3166_stse_transport.cpp") @objects "-Wl,--gc-sections" -o $executable
if ($LASTEXITCODE -ne 0) {
  throw "STSAFE native-transport test compilation failed."
}
& $executable
if ($LASTEXITCODE -ne 0) {
  throw "STSAFE native-transport tests failed."
}
