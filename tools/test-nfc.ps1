$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root "_build"
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($mode in @(0,16)) {
  $executable = Join-Path $output "nfc-test-$mode.exe"
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $root "tests\stubs") -I (Join-Path $root "src") `
    (Join-Path $root "tests\nfc_test.cpp") (Join-Path $root "src\GroveNfcTests.cpp") -o $executable
  if ($LASTEXITCODE -ne 0) { throw "NFC host-test compilation failed in mode $mode." }
  & $executable
  if ($LASTEXITCODE -ne 0) { throw "NFC host tests failed in mode $mode." }
  $uart = Join-Path $output "nfc-uart-test-$mode.exe"
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $root "tests\nfc_uart_stubs") -I (Join-Path $root "tests\stubs") -I (Join-Path $root "src") `
    (Join-Path $root "tests\nfc_uart_test.cpp") (Join-Path $root "src\NfcUart.cpp") -o $uart
  if ($LASTEXITCODE -ne 0) { throw "NFC UART test compilation failed in mode $mode." }
  & $uart
  if ($LASTEXITCODE -ne 0) { throw "NFC UART tests failed in mode $mode." }
}
