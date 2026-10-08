$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$library = Join-Path (Join-Path $projectRoot "libraries") "STSELib"
$metadata = Get-Content -LiteralPath (Join-Path $library "upstream.json") -Raw | ConvertFrom-Json
if ($metadata.commit -ne "01f494046ee1df593601e40371224969e20402d4" -or $metadata.tag -ne "v1.1.11" -or $metadata.license -ne "BSD-3-Clause") {
  throw "Unexpected STSELib source revision or license; review the pin and hash manifest together."
}
$count = 0
foreach ($line in Get-Content -LiteralPath (Join-Path $library "upstream-files.sha256")) {
  if ($line -notmatch '^([0-9a-f]{64})  (LICENSE\.txt|src/(stselib\.h|(api|certificate|core|services)/[A-Za-z0-9_./-]+))$') {
    throw "Invalid STSELib source manifest entry."
  }
  $expected = $Matches[1]
  $relative = $Matches[2]
  if ($relative.Contains("..")) {
    throw "Invalid STSELib source path."
  }
  $path = Join-Path $library ($relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash -ne $expected) {
    throw "Unreviewed change to pinned STSELib source: $relative"
  }
  $count++
}
if ($count -ne 109) {
  throw "Expected all 109 unmodified upstream runtime/license files."
}
Write-Output "PASS: all 109 STSELib v1.1.11 runtime/license files match the pinned source hashes."
