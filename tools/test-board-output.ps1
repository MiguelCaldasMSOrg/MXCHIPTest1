#requires -Version 7.2
$ErrorActionPreference = "Stop"

& {
  param($Tools)
  $observed = @{}
  function Write-Host {
    param([Parameter(Position=0)]$Object, $ForegroundColor)
    $observed.Message = [string]$Object
    $observed.Color = $ForegroundColor
  }
  . (Join-Path $Tools "BoardToolOutput.ps1")
  Write-BoardVerified "fixture completed."
  if ($observed.Message -cne "Verified: fixture completed." -or $observed.Color -cne "Green") {
    throw "Verified success must use the green host output channel."
  }
} $PSScriptRoot

foreach ($name in @("stlink-mass-storage.ps1","build-board-image.ps1","configure-wifi.ps1","provision-stsafe.ps1")) {
  $tokens = $null
  $errors = $null
  $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name), [ref]$tokens, [ref]$errors)
  if ($errors.Count) { throw "The $name script does not parse." }
  $calls = @($ast.FindAll({
    param($node)
    $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq "Write-BoardVerified"
  }, $true))
  if ($calls.Count -eq 0) { throw "$name must use the shared green verification helper." }
}
Write-Output "PASS: green Verified output shared across all four board-facing tools."
