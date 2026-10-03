$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot "_build"
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

foreach ($mode in @(0, 1)) {
  $executable = Join-Path $outputDirectory "rf_receiver_test-$mode.exe"
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_ENABLE_AUDIO_TESTS=$mode" -I (Join-Path $projectRoot "src") (Join-Path (Join-Path $projectRoot "tests") "rf_receiver_test.cpp") -o $executable
  if ($LASTEXITCODE -ne 0) {
    throw "RF test compilation failed for audio mode $mode with exit code $LASTEXITCODE."
  }
  & $executable
  if ($LASTEXITCODE -ne 0) {
    throw "RF tests failed for audio mode $mode with exit code $LASTEXITCODE."
  }
  $applicationTest = Join-Path $outputDirectory "application_test-$mode.exe"
  $sourceDirectory = Join-Path $projectRoot "src"
  $testDirectory = Join-Path $projectRoot "tests"
  $applicationSources = @(
    (Join-Path $testDirectory "application_test.cpp"),
    (Join-Path $sourceDirectory "AudioTests.cpp"),
    (Join-Path $sourceDirectory "RadioBridge.cpp")
  )
  & g++ -std=c++11 -Wall -Wextra -Werror -pedantic "-DMXCHIP_ENABLE_AUDIO_TESTS=$mode" -I (Join-Path $testDirectory "stubs") -I $sourceDirectory @applicationSources -o $applicationTest
  if ($LASTEXITCODE -ne 0) {
    throw "Application test compilation failed for audio mode $mode with exit code $LASTEXITCODE."
  }
  & $applicationTest
  if ($LASTEXITCODE -ne 0) {
    throw "Application tests failed for audio mode $mode with exit code $LASTEXITCODE."
  }
}
