#requires -Version 7.2
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "test-board-output.ps1")
& (Join-Path $PSScriptRoot "configure-wifi.ps1") -Port "FIXTURE-NEVER-OPEN" -Ssid ("X" * 100) -WhatIf
# The invalid port and SSID ensure the preview returns before validation, secret prompts or serial access.
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot "configure-wifi.ps1"), [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw "Wi-Fi console script does not parse." }
foreach ($name in @("ConvertTo-ConsoleArgument","Read-Until","Assert-ConsoleSave")) {
  $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
  if (-not $definition) { throw "Missing console helper: $name" }
  Set-Item -Path "Function:$name" -Value $definition.Body.GetScriptBlock()
}
if ((ConvertTo-ConsoleArgument "") -cne '""' -or (ConvertTo-ConsoleArgument 'a\b"c') -cne '"a\\b\"c"') {
  throw "Console quoting must preserve empty, backslash and quoted values."
}
$ack = "INFO: Set Wi-Fi password successfully."
Assert-ConsoleSave ("set_wifipwd contains-Invalid-word`r`n" + $ack) $ack Password
foreach ($response in @("Public-fixture-secret", "ERROR: Verify failed.`r`n$ack", "Invalid Wi-Fi password.`r`n$ack")) {
  $failure = $null
  try { Assert-ConsoleSave $response $ack Password } catch { $failure = $_ }
  if (-not $failure -or $failure.Exception.Message.Contains($response)) {
    throw "A console save must have an unambiguous acknowledgement and never expose the response."
  }
}
$serial = [pscustomobject]@{ Text = "Configuration console: fixture"; Reads = 0 }
$serial | Add-Member ScriptMethod ReadExisting { $this.Reads++; return $this.Text }
if ((Read-Until $serial @("Configuration console:")) -cne $serial.Text) { throw "Console identification failed." }
$serial.Text = "Public-fixture-secret-" * 450
$errorMessage = ""
try { Read-Until $serial @("not present") | Out-Null } catch { $errorMessage = $_.Exception.Message }
if (-not $errorMessage.Contains("Too much unexpected") -or $errorMessage.Contains("Public-fixture-secret-")) {
  throw "Oversized console output must fail without echoing response contents."
}
$errorMessage = ""
try { Read-Until $serial @("not present") -TimeoutSeconds 0 | Out-Null } catch { $errorMessage = $_.Exception.Message }
if (-not $errorMessage.Contains("Timed out") -or $errorMessage.Contains("Public-fixture-secret-")) {
  throw "Timeouts must fail without echoing credentials."
}
Write-Output "PASS: Wi-Fi confirmation/preview, quoting, exact save acknowledgement, bounded output and credential-safe errors; no serial port opened."
