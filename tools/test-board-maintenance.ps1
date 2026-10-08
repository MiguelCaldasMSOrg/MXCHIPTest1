#requires -Version 7.2
param([string]$VendorDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$module = Import-Module (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -PassThru -DisableNameChecking -Force

function Assert([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw "FAIL: $Message" }
}
function Reject([scriptblock]$Action, [string]$Message) {
  $rejected = $false
  try { & $Action | Out-Null } catch { $rejected = $true }
  Assert $rejected $Message
}
function Set-Word([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
  [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Buffer, $Offset, 4)
}

$uploadRecipe = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot "platform.local.txt") | Where-Object { $_.StartsWith("tools.openocd.upload.pattern=") })
Assert ($uploadRecipe.Count -eq 1) "one saved Arduino upload recipe"
Assert ($uploadRecipe[0].Contains('-f "interface\stlink.cfg"') -and $uploadRecipe[0].Contains('-c "transport select swd"') -and $uploadRecipe[0] -notmatch '(?i)hla') "native ST-Link interface and SWD transport remove both HLA deprecations"
$uploadCommand = [regex]::Match($uploadRecipe[0], '-c "(program .*; shutdown)"$')
Assert $uploadCommand.Success "upload recipe contains the expected programming command"
foreach ($buildPath in @("_build\supplied-keys", "C:\Build with spaces\supplied-keys")) {
  $expanded = $uploadCommand.Groups[1].Value.Replace("{build.path}", $buildPath).Replace("{build.project_name}", "MXCHIPTest1.ino")
  $expected = 'program {' + $buildPath + '\MXCHIPTest1.ino.bin} verify reset 0x0800C000; shutdown'
  Assert ($expanded -ceq $expected) "exactly one Tcl filename brace pair, including paths with spaces"
}
Write-Output "PASS: native ST-Link/SWD Arduino upload pairing, filename quoting and application address."

$prefix = [byte[]]::new(0xC000)
for ($index = 0; $index -lt $prefix.Length; $index++) { $prefix[$index] = ($index * 73 + 19) % 256 }
Set-Word $prefix 0 0x20040000
Set-Word $prefix 4 0x08000035
$app = [byte[]]::new(32)
Set-Word $app 0 0x20040000
Set-Word $app 4 0x0800C009
for ($index = 8; $index -lt $app.Length; $index++) { $app[$index] = $index }
$image = Join-MxBoardImage $prefix $prefix $app
Assert ($image.Length -eq 0xC000 + $app.Length) "exact image shape"
Assert ((Get-MxHash $image[0..(0xC000-1)]) -eq (Get-MxHash $prefix)) "every prefix byte is from the board"
Assert ((Get-MxHash $image[0xC000..($image.Length-1)]) -eq (Get-MxHash $app)) "every application byte is unchanged"
Assert ((Get-MxHash $image[0x8000..0xBFFF]) -eq (Get-MxHash $prefix[0x8000..0xBFFF])) "the entire host-key sector is retained, not synthesized"
$changed = [byte[]]$prefix.Clone()
$changed[0x8000] = $changed[0x8000] -bxor 1
Reject { Join-MxBoardImage $prefix $changed $app } "mismatched reads must fail"
Reject { Join-MxBoardImage $prefix ([byte[]]::new(1)) $app } "short prefix must fail"
$invalid = [byte[]]$prefix.Clone()
Set-Word $invalid 4 0x0800C009
Reject { Join-MxBoardImage $invalid $invalid $app } "an application is not a bootloader prefix"
Reject { Assert-MxImage ([byte[]]::new(0)) } "empty application rejected"
Reject { Assert-MxImage ([byte[]]::new(0x100000 - 0xC000 + 1)) } "oversized application rejected"
$serial = "066CFF515254667867131724"
$cfg = New-MxSnapshotConfiguration $serial "374B"
Assert ($cfg.Contains("hla_serial $serial") -and $cfg.Contains("hla_vid_pid 0x0483 0x374B")) "exact probe selection"
Assert ($cfg.Contains("dump_image prefix-a.bin 0x08000000 0xC000") -and $cfg.Contains("dump_image prefix-b.bin 0x08000000 0xC000")) "two full reads from the same halted target"
Assert ($cfg.Contains('if {$previous == "running"} { resume }')) "run/halt state restoration"
Assert ($cfg -notmatch '(?im)^\s*(program|flash\s+(erase|write|protect)|reset(\s|$)|stm32f2x\s+unlock)') "no destructive commands in snapshot"
Assert ($cfg.Contains("RDP is enabled") -and $cfg.Contains("PCROP mode is enabled")) "readout-protection fails closed"
Reject { New-MxSnapshotConfiguration 'x"; program evil' "374B" } "Tcl injection blocked"
Reject { New-MxSnapshotConfiguration $serial "1234" } "unrelated USB IDs blocked"
$restore = New-MxSnapshotRecoveryConfiguration $serial "374B" "running 00000004"
Assert ($restore.Contains("`nresume`n") -and $restore.Contains("mww 0xE0042008 0x00000004") -and -not $restore.Contains("dump_image")) "recovery only restores execution/debug state"
Reject { New-MxSnapshotRecoveryConfiguration $serial "374B" "running 0;reset" } "recovery state injection blocked"
$modern = New-MxSnapshotConfiguration $serial "374B" -InterfaceScript "interface\stlink-hla.cfg"
$modernRestore = New-MxSnapshotRecoveryConfiguration $serial "374B" "halted 00000004" -InterfaceScript "interface\stlink-hla.cfg"
Assert ($modern.Contains('source [find {interface\stlink-hla.cfg}]') -and $modernRestore.Contains('source [find {interface\stlink-hla.cfg}]')) "selected HLA interface is used for snapshot and state restoration"
Reject { New-MxSnapshotConfiguration $serial "374B" -InterfaceScript 'bad.cfg}; program evil' } "interface selection rejects Tcl injection"

foreach ($state in @("Enabled", "Disabled")) {
  $args = @(New-MxMscArguments $serial $state)
  $option = if ($state -eq "Enabled") { "mscOnOpt" } else { "mscOffOpt" }
  Assert (($args -join ' ') -ceq "-sn $serial -msvcp -dynOpt $option -force_prog") "exact reversible vendor operation"
  $preflight = @(New-MxMscArguments $serial $state -Preflight)
  Assert ($preflight -contains "-checkParam" -and $preflight -notcontains "-force_prog") "preflight cannot program firmware"
  Assert ($args -notcontains "mscAlwaysOff" -and $args -notcontains "-jtag") "never permanently remove MSC/VCP"
}
$probe = [pscustomobject]@{ Status="OK"; ChildrenHealthy=$true; DebugPresent=$true; SerialPresent=$true; MassStoragePresent=$false }
Assert (Test-MxMscState $probe "Disabled") "complete non-MSC enumeration accepted"
$probe.DebugPresent = $false
Assert (-not (Test-MxMscState $probe "Disabled")) "disappearing debug interface cannot count as success"
$probe.DebugPresent = $true
$probe.ChildrenHealthy = $false
Assert (-not (Test-MxMscState $probe "Disabled")) "driver errors cannot count as success"
$probe.ChildrenHealthy = $true
$probe.MassStoragePresent = $true
Assert (-not (Test-MxMscState $probe "Disabled")) "Windows-hidden but present storage is not disabled firmware"
Assert (Test-MxMscState $probe "Enabled") "reenabled interface requires positive evidence"
$attributes = @(ConvertFrom-MxJarAttributes "Manifest-Version: 1.0`r`nSHA-256-Digest: abc`r`n def`r`n`r`nName: data`r`nSHA-256-Digest: x`r`n")
Assert ($attributes.Count -eq 2 -and $attributes[0]["SHA-256-Digest"] -ceq "abcdef") "JAR folded attributes"
Reject { ConvertFrom-MxJarAttributes "Name: a`nName: b`n" } "duplicate JAR attributes rejected"
Reject { ConvertFrom-MxJarAttributes "invalid" } "malformed JAR attributes rejected"

$fixture = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ("mxchip-board-tools-" + [Guid]::NewGuid().ToString("N")))).FullName
try {
  $reloadRoot = Join-Path $fixture "module-reload"
  $null = New-Item -ItemType Directory -Path $reloadRoot
  $reloadModulePath = Join-Path $reloadRoot "BoardMaintenance.psm1"
  $reloadEntryPath = Join-Path $reloadRoot "entry.ps1"
  foreach ($entry in @("build-board-image.ps1", "stlink-mass-storage.ps1", "validate-firmware.ps1")) {
    $tokens = $null
    $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $entry), [ref]$tokens, [ref]$errors)
    Assert ($errors.Count -eq 0) "$entry parses"
    $imports = @($ast.FindAll({
      param($node)
      $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq "Import-Module" -and $node.Extent.Text.Contains("BoardMaintenance.psm1")
    }, $true))
    Assert ($imports.Count -eq 1) "$entry has one maintenance module import"
    [IO.File]::WriteAllText($reloadModulePath, 'function Get-MxReloadFixtureVersion { return 1 }; Export-ModuleMember -Function Get-MxReloadFixtureVersion')
    $cached = Microsoft.PowerShell.Core\Import-Module $reloadModulePath -PassThru -DisableNameChecking -Force
    try {
      Assert ((& $cached { Get-MxReloadFixtureVersion }) -eq 1) "initial module version loaded"
      [IO.File]::WriteAllText($reloadModulePath, 'function Get-MxReloadFixtureVersion { return 2 }; Export-ModuleMember -Function Get-MxReloadFixtureVersion')
      [IO.File]::WriteAllText($reloadEntryPath, $imports[0].Extent.Text + "`nGet-MxReloadFixtureVersion")
      $version = & $reloadEntryPath
      Assert ($version -eq 2) "$entry reloads updated module code rather than reusing the cached version"
    } finally {
      Get-Module -All | Where-Object { $_.Path -eq $reloadModulePath } | Remove-Module -Force
    }
  }
  Write-Output "PASS: all maintenance entry points reload updated module code in an existing PowerShell session."

  if ($IsWindows) {
    $originalInvoker = & $module { (Get-Item Function:Invoke-MxTool).ScriptBlock }
    & $module {
      $script:configurationFailure = $false
      $script:configurationMissingAck = $false
      $script:configurationInterface = ""
      function script:Invoke-MxTool {
        param($FilePath,$Arguments,$WorkingDirectory,$TimeoutSeconds,[switch]$NeverKill)
        if (($Arguments -join ' ') -eq "config get directories.data --json") {
          return [pscustomobject]@{ ExitCode=0; Text=($script:fakeArduinoData | ConvertTo-Json -Compress) }
        }
        if ($Arguments[-1] -ceq "shutdown") {
          if ($Arguments.Count -ne 12 -or $Arguments[0] -cne "-s" -or $Arguments[2] -cne "-f" -or
              $Arguments[4] -cne "-c" -or $Arguments[5] -cne "transport select hla_swd" -or
              $Arguments[6] -cne "-f" -or $Arguments[7] -cne "target\stm32f4x.cfg" -or
              $Arguments[8] -cne "-c" -or $Arguments[9] -cne "echo MXCHIP_HLA_CONFIG_OK" -or $Arguments[10] -cne "-c") {
            throw "Expected only configuration parsing followed by shutdown, never init or target access."
          }
          $script:configurationInterface = $Arguments[3]
          if ($script:configurationFailure) { return [pscustomobject]@{ ExitCode=1; Text="Debug adapter does not support hla_swd" } }
          return [pscustomobject]@{ ExitCode=0; Text=$(if ($script:configurationMissingAck) { "shutdown" } else { "MXCHIP_HLA_CONFIG_OK" }) }
        }
        if (($Arguments -join ' ') -ne "--version") { throw "OpenOCD layout discovery must not touch the board." }
        [pscustomobject]@{ ExitCode=0; Text="xPack Open On-Chip Debugger 0.12.0+dev" }
      }
    }
    try {
      $index = 0
      foreach ($relative in @("scripts", "openocd\scripts", "share\openocd\scripts")) {
        $root = Join-Path $fixture ("openocd-layout-" + $index++)
        $scripts = Join-Path $root $relative
        $null = New-Item -ItemType Directory -Path (Join-Path $root "bin"),(Join-Path $scripts "interface"),(Join-Path $scripts "target") -Force
        foreach ($file in @((Join-Path $root "bin\openocd.exe"),(Join-Path $scripts "interface\stlink-v2-1.cfg"),(Join-Path $scripts "target\stm32f4x.cfg"))) {
          [IO.File]::WriteAllText($file, "test fixture, never executed")
        }
        $resolved = Resolve-MxOpenOcd -Root $root -UsbPid "3752"
        Assert ($resolved.Scripts -eq $scripts -and $resolved.InterfaceScript -ceq "interface\stlink-v2-1.cfg") "legacy HLA layout recognized: $relative"
        [IO.File]::WriteAllText((Join-Path $scripts "interface\stlink-hla.cfg"), "modern explicit HLA fixture")
        $resolved = Resolve-MxOpenOcd -Root $root -UsbPid "3752"
        Assert ($resolved.InterfaceScript -ceq "interface\stlink-hla.cfg" -and (& $module { $script:configurationInterface }) -ceq $resolved.InterfaceScript) "explicit HLA file preferred over the native legacy-name alias: $relative"
      }
      & $module { $script:configurationFailure = $true }
      Reject { Resolve-MxOpenOcd -Root $root -UsbPid "3752" } "unsupported HLA transport rejected before target access"
      & $module { $script:configurationFailure = $false; $script:configurationMissingAck = $true }
      Reject { Resolve-MxOpenOcd -Root $root -UsbPid "3752" } "missing configuration acknowledgement rejected"
      & $module { $script:configurationMissingAck = $false }
      Remove-Item -LiteralPath (Join-Path $scripts "target\stm32f4x.cfg")
      Reject { Resolve-MxOpenOcd -Root $root -UsbPid "3752" } "incomplete script installation rejected"
      Write-Output "PASS: legacy/xPack/standard layouts, explicit HLA selection and configuration-only transport validation."
      $dataRoot = Join-Path $fixture "arduino-data"
      $bundledRoot = Join-Path $dataRoot "packages\AZ3166\tools\openocd\0.10.0"
      $bundledScripts = Join-Path $bundledRoot "scripts"
      $null = New-Item -ItemType Directory -Path (Join-Path $bundledRoot "bin"),(Join-Path $bundledScripts "interface"),(Join-Path $bundledScripts "target") -Force
      foreach ($file in @((Join-Path $bundledRoot "bin\openocd.exe"),(Join-Path $bundledScripts "interface\stlink-v2-1.cfg"),(Join-Path $bundledScripts "target\stm32f4x.cfg"))) {
        [IO.File]::WriteAllText($file, "test fixture, never executed")
      }
      & $module { param($Data) $script:fakeArduinoData = $Data } $dataRoot
      $resolved = Resolve-MxOpenOcd -UsbPid "374B"
      Assert ($resolved.Scripts -eq $bundledScripts) "Arduino effective setting works without a configuration-dump directories object"
      & $module { $script:fakeArduinoData = "relative-data" }
      Reject { Resolve-MxOpenOcd -UsbPid "374B" } "invalid effective data directory rejected"
      Write-Output "PASS: effective Arduino data-directory discovery and malformed-setting rejection."
    } finally {
      & $module { param($Original) Set-Item Function:script:Invoke-MxTool -Value $Original } $originalInvoker
    }
  }
  if ($VendorDirectory) {
    $verified = Test-MxUpdaterPackage $VendorDirectory
    Assert ($verified.SignerSha256 -eq "7B35ED6E0BD638A2E4D376A4E7E2F4AF30294A1123127EB5FD7578F7B08A4B93") "ST signer pin"
    $packageRoot = Join-Path $fixture "vendor-input"
    $platform = Join-Path $packageRoot "stsw-link007\AllPlatforms"
    $native = Join-Path $platform "native\win_x64"
    $null = New-Item -Path $native -ItemType Directory -Force
    Copy-Item -LiteralPath (Join-Path $VendorDirectory "STLinkUpgrade.jar") -Destination $platform
    Copy-Item -LiteralPath (Join-Path $VendorDirectory "native\win_x64\STLinkUSBDriver.dll") -Destination $native
    $archivePath = Join-Path $fixture "vendor.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $archivePath)
    $installed = Join-Path $fixture "verified-setup"
    $mscScript = Join-Path $PSScriptRoot "stlink-mass-storage.ps1"
    Reject { & $mscScript -Action Setup -VendorZip $archivePath -ToolDirectory $installed -Confirm:$false } "setup requires explicit vendor-license acknowledgement"
    & $mscScript -Action Setup -VendorZip $archivePath -ToolDirectory $installed -AcceptVendorLicense -Confirm:$false | Out-Null
    Assert ((Test-MxUpdaterPackage $installed).JarSha256 -eq $verified.JarSha256) "self-contained setup copies exactly the authenticated updater"
    $zip = [IO.Compression.ZipFile]::Open((Join-Path $installed "STLinkUpgrade.jar"), [IO.Compression.ZipArchiveMode]::Update)
    try {
      $name = "com/st/stlinkupgrade/app/MainApp.class"
      $payload = Read-MxZipEntry $zip $name
      $payload[0] = $payload[0] -bxor 1
      $zip.GetEntry($name).Delete()
      $stream = $zip.CreateEntry($name).Open()
      try { $stream.Write($payload, 0, $payload.Length) } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
    Reject { Test-MxUpdaterPackage $installed } "changed signed payload rejected even with the original manifest and signature"
    Write-Output "PASS: vendor setup, signatures and tampered-payload rejection without executing Java or the native USB driver."
  }
  if ($IsWindows) {
    $appFile = Join-Path $fixture "application.bin"
    [IO.File]::WriteAllBytes($appFile, $app)
    # Keep the simulated device functions installed during workflow tests; reload behavior is tested separately above.
    function Import-Module {
      [CmdletBinding()]
      param(
        [Parameter(Position=0, Mandatory)][string]$Name,
        [switch]$DisableNameChecking,
        [switch]$Force
      )
      if ([IO.Path]::GetFullPath($Name) -ne $module.Path -or -not $Force) {
        throw "Workflow test expected an explicit forced import of the maintenance module."
      }
      Microsoft.PowerShell.Core\Import-Module -Name $Name -DisableNameChecking:$DisableNameChecking
    }
    & $module {
      param($Prefix, $App)
      $script:fakePrefix = $Prefix
      $script:fakeApp = $App
      $script:fakeFault = ""
      $script:fakeCalls = 0
      function script:Get-MxStLink {
        param($SerialNumber)
        [pscustomobject]@{ SerialNumber="066CFF515254667867131724"; UsbPid="374B"; InstanceId="fixture"; Status="OK"; ChildrenHealthy=$true; DebugPresent=$true; SerialPresent=$true; MassStoragePresent=$true }
      }
      function script:Resolve-MxOpenOcd {
        param($Root,$UsbPid)
        [pscustomobject]@{ Exe="fixture.exe"; Scripts="fixture"; InterfaceScript="interface\stlink-hla.cfg"; Version="fixture OpenOCD" }
      }
      function script:Invoke-MxTool {
        param($FilePath,$Arguments,$WorkingDirectory,$TimeoutSeconds,[switch]$NeverKill)
        $script:fakeCalls++
        if (-not [IO.File]::ReadAllText($Arguments[-1]).Contains('source [find {interface\stlink-hla.cfg}]')) {
          throw "Snapshot and restoration must retain the resolved HLA interface."
        }
        if (($Arguments -join " ") -match 'recover.cfg') {
          return [pscustomobject]@{ ExitCode=0; Text="MXCHIP_RECOVERY_OK`n" }
        }
        [IO.File]::WriteAllText((Join-Path $WorkingDirectory "state.txt"), "running 00000000")
        [IO.File]::WriteAllBytes((Join-Path $WorkingDirectory "prefix-a.bin"), $script:fakePrefix)
        $second = [byte[]]$script:fakePrefix.Clone()
        if ($script:fakeFault -eq "mismatch") { $second[100] = $second[100] -bxor 1 }
        [IO.File]::WriteAllBytes((Join-Path $WorkingDirectory "prefix-b.bin"), $second)
        [IO.File]::WriteAllBytes((Join-Path $WorkingDirectory "target-uid.bin"), [byte[]](1..12))
        if ($script:fakeFault -eq "process") { return [pscustomobject]@{ ExitCode=1; Text="fixture failure" } }
        if ($script:fakeFault -eq "ack") { return [pscustomobject]@{ ExitCode=0; Text="no acknowledgement" } }
        return [pscustomobject]@{ ExitCode=0; Text="MXCHIP_SNAPSHOT_OK`n" }
      }
    } $prefix $app
    $scriptPath = Join-Path $PSScriptRoot "build-board-image.ps1"
    $output = Join-Path $fixture "success"
    & $scriptPath -Application $appFile -OutputDirectory $output -Confirm:$false | Out-Null
    $files = @(Get-ChildItem -LiteralPath $output -File)
    Assert ($files.Count -eq 2) "only image and manifest remain; raw dumps removed"
    $record = Get-Content -LiteralPath (Join-Path $output "manifest.json") -Raw | ConvertFrom-Json
    Assert ($record.confidential -and $record.prefixLength -eq 49152 -and $record.imageLength -eq $image.Length) "confidential provenance metadata"
    Assert ((Get-FileHash -LiteralPath (Join-Path $output $record.imageFile)).Hash -eq (Get-MxHash $image)) "full workflow publishes exact combined bytes"
    Assert ((Get-Acl -LiteralPath $output).AreAccessRulesProtected) "output ACL inheritance is disabled"
    foreach ($fault in @("mismatch", "process", "ack")) {
      & $module { param($Fault) $script:fakeFault = $Fault } $fault
      $failedOutput = Join-Path $fixture $fault
      Reject { & $scriptPath -Application $appFile -OutputDirectory $failedOutput -Confirm:$false } "workflow rejects $fault"
      Assert (@(Get-ChildItem -LiteralPath $failedOutput -File).Count -eq 0) "failed workflow leaves no raw dumps or success-shaped image"
    }
    $preview = Join-Path $fixture "preview"
    $before = & $module { $script:fakeCalls }
    & $scriptPath -Application $appFile -OutputDirectory $preview -WhatIf | Out-Null
    Assert (-not (Test-Path -LiteralPath $preview) -and (& $module { $script:fakeCalls }) -eq $before) "WhatIf never invokes OpenOCD or creates output"
    Reject { & $scriptPath -Application $appFile -OutputDirectory $output -Confirm:$false } "existing output never overwritten"
    Write-Output "PASS: private image workflow with simulated OpenOCD, target recovery, errors, cleanup and WhatIf."

    & $module {
      $script:mscPresent = $true
      $script:mscCalls = @()
      $script:mscFault = ""
      function script:Get-MxStLink {
        param($SerialNumber)
        [pscustomobject]@{ SerialNumber="066CFF515254667867131724"; UsbPid=$(if ($script:mscPresent) { "374B" } else { "3752" }); InstanceId="fixture"; Status="OK"; ChildrenHealthy=$true; DebugPresent=$true; SerialPresent=$true; MassStoragePresent=$script:mscPresent }
      }
      function script:Test-MxUpdaterPackage {
        param($Directory)
        [pscustomobject]@{ Jar="fixture.jar"; JarSha256="ABC"; DriverSha256="DEF"; SignerSha256="fixture" }
      }
      function script:Invoke-MxTool {
        param($FilePath,$Arguments,$WorkingDirectory,$TimeoutSeconds,[switch]$NeverKill)
        $script:mscCalls += ,$Arguments
        if ($Arguments -contains "-checkParam") {
          return [pscustomobject]@{ ExitCode=0; Text=$(if ($script:mscFault -eq "preflight") { "Parameter error: incompatible" } else { "Parameters checked" }) }
        }
        if ($Arguments -notcontains "-force_prog" -or -not $NeverKill -or $Arguments -contains "mscAlwaysOff") { throw "Invalid mock updater operation" }
        if ($script:mscFault -eq "updater") { return [pscustomobject]@{ ExitCode=1; Text="firmware update failed" } }
        $script:mscPresent = $Arguments -contains "mscOnOpt"
        return [pscustomobject]@{ ExitCode=0; Text="Upgrade successful" }
      }
    }
    $mscScript = Join-Path $PSScriptRoot "stlink-mass-storage.ps1"
    $javaFixture = Join-Path $PSHOME "pwsh.exe"
    & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -WhatIf | Out-Null
    Assert ((& $module { $script:mscCalls.Count }) -eq 0) "MSC WhatIf never loads the vendor tool"
    & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false | Out-Null
    Assert (-not (& $module { $script:mscPresent })) "disable workflow changes the simulated BOARD personality"
    & $mscScript -Action Enabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false | Out-Null
    Assert ((& $module { $script:mscPresent })) "reenable workflow does not depend on OpenOCD/PID 374B"
    $count = & $module { $script:mscCalls.Count }
    & $mscScript -Action Enabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false | Out-Null
    Assert ((& $module { $script:mscCalls.Count }) -eq $count) "already-correct state needs no firmware write"
    foreach ($fault in @("preflight", "updater")) {
      & $module { param($Fault) $script:mscFault = $Fault } $fault
      Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "vendor $fault failure surfaced"
      Assert ((& $module { $script:mscPresent })) "failed update cannot report a successful switch"
    }
    Write-Output "PASS: simulated board-side MSC disable/reenable, package preflight, idempotence and error paths; no vendor updater executed."
  }
} finally {
  if (Test-Path Function:Import-Module) { Remove-Item Function:Import-Module }
  Remove-Module $module -Force
  Remove-Item -LiteralPath $fixture -Recurse -Force
}
Write-Output "PASS: exact image/host-sector preservation, snapshot command safety, reversible MSC options, USB-state verification and signed-manifest parsing."
