$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot "_build"
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

foreach ($mode in 0..16) {
  $executable = Join-Path $outputDirectory "rf_receiver_test-$mode.exe"
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $projectRoot "src") (Join-Path (Join-Path $projectRoot "tests") "rf_receiver_test.cpp") -o $executable
  if ($LASTEXITCODE -ne 0) {
    throw "RF test compilation failed for mode $mode with exit code $LASTEXITCODE."
  }
  & $executable
  if ($LASTEXITCODE -ne 0) {
    throw "RF tests failed for mode $mode with exit code $LASTEXITCODE."
  }
  $applicationTest = Join-Path $outputDirectory "application_test-$mode.exe"
  $sourceDirectory = Join-Path $projectRoot "src"
  $testDirectory = Join-Path $projectRoot "tests"
  $applicationSources = @(
    (Join-Path $testDirectory "application_test.cpp"),
    (Join-Path $sourceDirectory "AudioTests.cpp"),
    (Join-Path $sourceDirectory "RadioBridge.cpp"),
    (Join-Path $sourceDirectory "RadioDisplay.cpp")
  )
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $testDirectory "stubs") -I $sourceDirectory @applicationSources -o $applicationTest
  if ($LASTEXITCODE -ne 0) {
    throw "Application test compilation failed for mode $mode with exit code $LASTEXITCODE."
  }
  & $applicationTest
  if ($LASTEXITCODE -ne 0) {
    throw "Application tests failed for mode $mode with exit code $LASTEXITCODE."
  }
  $loraTest = Join-Path $outputDirectory "lora_bridge_test-$mode.exe"
  $loraSources = @(
    (Join-Path $testDirectory "lora_bridge_test.cpp"),
    (Join-Path $sourceDirectory "LoRaBridge.cpp"),
    (Join-Path $sourceDirectory "RadioDisplay.cpp")
  )
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $testDirectory "stubs") -I $sourceDirectory @loraSources -o $loraTest
  if ($LASTEXITCODE -ne 0) {
    throw "LoRa integration test compilation failed for mode $mode with exit code $LASTEXITCODE."
  }
  & $loraTest
  if ($LASTEXITCODE -ne 0) {
    throw "LoRa integration tests failed for mode $mode with exit code $LASTEXITCODE."
  }
}

$loraProtocolTest = Join-Path $outputDirectory "lora_test.exe"
& g++ -std=c++11 -Wall -Wextra -Werror -pedantic -I (Join-Path $projectRoot "src") (Join-Path (Join-Path $projectRoot "tests") "lora_test.cpp") -o $loraProtocolTest
if ($LASTEXITCODE -ne 0) {
  throw "LoRa UART/protocol test compilation failed with exit code $LASTEXITCODE."
}
& $loraProtocolTest
if ($LASTEXITCODE -ne 0) {
  throw "LoRa UART/protocol tests failed with exit code $LASTEXITCODE."
}

foreach ($mode in @(3, 4)) {
  $rtcTest = Join-Path $outputDirectory "rtc_test-$mode.exe"
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_TEST_MODE=$mode" -I (Join-Path $testDirectory "stubs") -I $sourceDirectory (Join-Path $testDirectory "rtc_test.cpp") (Join-Path $sourceDirectory "RtcTests.cpp") (Join-Path $sourceDirectory "RtcClock.cpp") -o $rtcTest
  if ($LASTEXITCODE -ne 0) {
    throw "RTC test compilation failed for mode $mode with exit code $LASTEXITCODE."
  }
  & $rtcTest
  if ($LASTEXITCODE -ne 0) {
    throw "RTC tests failed for mode $mode with exit code $LASTEXITCODE."
  }
}
