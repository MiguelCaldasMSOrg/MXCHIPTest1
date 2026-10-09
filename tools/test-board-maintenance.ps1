#requires -Version 7.2
param([string]$VendorDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "test-board-output.ps1")
$module = Import-Module (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -PassThru -DisableNameChecking -Force

function Assert([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw "FAIL: $Message" }
}
function Reject([scriptblock]$Action, [string]$Message, [string]$ExpectedError = "") {
  $failure = $null
  try { & $Action | Out-Null } catch { $failure = $_ }
  Assert ($null -ne $failure) $Message
  if ($ExpectedError) {
    Assert ($failure.Exception.Message -match $ExpectedError) "$Message (unexpected error: $($failure.Exception.Message))"
  }
}
function Set-Word([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
  [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Buffer, $Offset, 4)
}

$vendorRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "drivers"
foreach ($asset in @(
  @{ Name="stsw-link007.zip"; Bytes=4973107; Hash="51B76FCBF6B417D03C7CBFC9F029A2D1F463BD0200EE8F3D80764D45D735EE1C" },
  @{ Name="libusb-1.0.23.tar.bz2"; Bytes=602860; Hash="DB11C06E958A82DAC52CF3C65CB4DD2C3F339C8A988665110E0D24D19312AD8D" },
  @{ Name="libusb-1.0.27.tar.bz2"; Bytes=643680; Hash="FFAA41D741A8A3BEE244AC8E54A72EA05BF2879663C098C82FC5757853441575" }
)) {
  $file = Get-Item -LiteralPath (Join-Path $vendorRoot $asset.Name)
  Assert (-not $file.PSIsContainer -and $file.Length -eq $asset.Bytes -and (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash -ceq $asset.Hash) "unchanged vendor/source archive: $($asset.Name)"
}
Assert (Test-Path -LiteralPath (Join-Path $vendorRoot "STSW-LINK007-LICENSE.txt") -PathType Leaf) "STSW-LINK007 license accompanies the archive"
Write-Output "PASS: bundled STSW-LINK007 archive, corresponding libusb source archives and ST license."

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
  Assert ($args -notcontains "-checkParam") "one vendor invocation, without a separate loader-changing parameter check"
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

$digestBytes = [Text.Encoding]::ASCII.GetBytes("Public JAR digest fixture")
$sha256 = [Convert]::ToBase64String([Security.Cryptography.SHA256]::HashData($digestBytes))
$sha384 = [Convert]::ToBase64String([Security.Cryptography.SHA384]::HashData($digestBytes))
foreach ($suffix in @("Digest", "Digest-Manifest")) {
  foreach ($algorithm in @("SHA-256", "SHA-384")) {
    $digestAttributes = @{ "$algorithm-$suffix" = $(if ($algorithm -eq "SHA-256") { $sha256 } else { $sha384 }) }
    & $module { param($Attributes,$Suffix,$Bytes) Assert-MxJarDigest $Attributes $Suffix $Bytes "Test fixture" } $digestAttributes $suffix $digestBytes
    $tampered = [byte[]]$digestBytes.Clone()
    $tampered[0] = $tampered[0] -bxor 1
    Reject { & $module { param($Attributes,$Suffix,$Bytes) Assert-MxJarDigest $Attributes $Suffix $Bytes "Test fixture" } $digestAttributes $suffix $tampered } "tampered $algorithm $suffix rejected" "digest mismatch"
  }
  $both = @{ "SHA-256-$suffix"=$sha256; "SHA-384-$suffix"=$sha384 }
  & $module { param($Attributes,$Suffix,$Bytes) Assert-MxJarDigest $Attributes $Suffix $Bytes "Test fixture" } $both $suffix $digestBytes
  $both["SHA-256-$suffix"] = "invalid"
  Reject { & $module { param($Attributes,$Suffix,$Bytes) Assert-MxJarDigest $Attributes $Suffix $Bytes "Test fixture" } $both $suffix $digestBytes } "one valid digest does not hide another invalid digest" "SHA-256 digest mismatch"
  Reject { & $module { param($Suffix,$Bytes) Assert-MxJarDigest @{} $Suffix $Bytes "Test fixture" } $suffix $digestBytes } "missing digest rejected" "no supported"
  Reject { & $module { param($Suffix,$Bytes) Assert-MxJarDigest @{ "SHA1-$Suffix"="invalid" } $Suffix $Bytes "Test fixture" } $suffix $digestBytes } "unsupported weak digest rejected" "no supported"
}
Write-Output "PASS: SHA-256/SHA-384 signed-manifest and member digests, tampering and unsupported-algorithm rejection."

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
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
      $principal = [Security.Principal.WindowsPrincipal]::new($identity)
      $actualAdministrator = Test-MxAdministrator
      Assert ($actualAdministrator -is [bool] -and $actualAdministrator -eq $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) "elevation check reads the current Windows access token"
    } finally {
      $identity.Dispose()
    }
    & $module {
      function script:Get-PnpDevice {
        [CmdletBinding()]
        param([switch]$PresentOnly)
        [pscustomobject]@{ InstanceId="USB\VID_0483&PID_3748\066CFF515254667867131724"; Status="OK" }
      }
      function script:Get-PnpDeviceProperty {
        [CmdletBinding()]
        param($InstanceId,$KeyName)
        if ($InstanceId -cne "USB\VID_0483&PID_3748\066CFF515254667867131724" -or $KeyName -cne "DEVPKEY_Device_LocationPaths") {
          throw "Unexpected transition-device metadata request."
        }
        [pscustomobject]@{ Data=@("fixture-usb-location","fixture-alternate-location") }
      }
    }
    try {
      Reject { & $module { Get-MxStLink -SerialNumber "066CFF515254667867131724" } } "loader identity is rejected with explicit reconnect guidance" "PID 3748.*USB loader mode"
      Reject { & $module { Get-MxStLink -SerialNumber "000000000000000000000000" } } "an unrelated loader cannot be mistaken for the requested board" "found 0"
      $transition = & $module { Get-MxStLinkTransitionDevice -SerialNumber "066CFF515254667867131724" }
      Assert ($transition.UsbPid -ceq "3748" -and $transition.LocationPath -ceq "fixture-usb-location") "transition inventory binds the exact serial to its physical USB location"
      Assert ($null -eq (& $module { Get-MxStLinkTransitionDevice -SerialNumber "000000000000000000000000" })) "transition inventory never substitutes an unrelated probe"
    } finally {
      & $module { Remove-Item Function:script:Get-PnpDevice; Remove-Item Function:script:Get-PnpDeviceProperty }
    }
    Write-Output "PASS: non-application ST-Link identity detection without vendor or hardware access."

    $originalInvoker = & $module { (Get-Item Function:Invoke-MxTool).ScriptBlock }
    & $module {
      $script:configurationFailure = $false
      $script:configurationMissingAck = $false
      $script:configurationInterface = ""
      function script:Invoke-MxTool {
        param($FilePath,$Arguments,$WorkingDirectory,$TimeoutSeconds,[switch]$NeverKill)
        if (($Arguments -join ' ') -eq "board details --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 --show-properties=expanded") {
          return [pscustomobject]@{ ExitCode=0; Text=$script:fakeOpenOcdProperties }
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
      & $module { param($Root) $script:fakeOpenOcdProperties = "tools.openocd.path=$Root`ntools.openocd.cmd=bin\openocd.exe`n" } $bundledRoot
      $resolved = Resolve-MxOpenOcd -UsbPid "374B"
      Assert ($resolved.Scripts -eq $bundledScripts) "board-image discovery uses the configured Arduino executable"
      $modernRoot = Join-Path $fixture "openocd-layout-1"
      & $module { param($Root) $script:fakeOpenOcdProperties = "tools.openocd.path=$Root\openocd`ntools.openocd.cmd=..\bin\openocd.exe`n" } $modernRoot
      $resolved = Resolve-MxOpenOcd -UsbPid "3752"
      Assert ($resolved.Exe -eq (Join-Path $modernRoot "bin\openocd.exe") -and $resolved.InterfaceScript -eq "interface\stlink-hla.cfg") "xPack override is selected instead of the core's installed package"
      & $module { $script:fakeOpenOcdProperties = "tools.openocd.path=relative`ntools.openocd.cmd=bin\openocd.exe" }
      Reject { Resolve-MxOpenOcd -UsbPid "374B" } "invalid configured executable rejected" "unambiguous absolute"
      & $module { $script:fakeOpenOcdProperties = "tools.openocd.path=C:\one`ntools.openocd.path=C:\two`ntools.openocd.cmd=bin\openocd.exe" }
      Reject { Resolve-MxOpenOcd -UsbPid "374B" } "ambiguous configured executable rejected" "unambiguous absolute"
      Write-Output "PASS: effective Arduino OpenOCD selection, xPack overrides and invalid/ambiguous-setting rejection."
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
    # Setup reloads the real module; mocks must bind to the instance used by entry points.
    $module = Microsoft.PowerShell.Core\Import-Module (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -Force -PassThru -DisableNameChecking
    $originalProbe = & $module { (Get-Item Function:Get-MxStLink).ScriptBlock }
    & $module {
      $script:elevationFault = ""
      $script:lastElevation = $null
      function script:Start-Process {
        [CmdletBinding()]
        param($FilePath,$ArgumentList,$Verb,[switch]$PassThru)
        if ($FilePath -cne (Join-Path $PSHOME "pwsh.exe") -or $Verb -cne "RunAs" -or -not $PassThru -or
            $ArgumentList.Count -ne 4 -or $ArgumentList[0] -cne "-NoProfile" -or
            $ArgumentList[1] -cne "-NonInteractive" -or $ArgumentList[2] -cne "-EncodedCommand") {
          throw "Unexpected elevation launch."
        }
        $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($ArgumentList[3]))
        $tokens = $null
        $errors = $null
        $null = [Management.Automation.Language.Parser]::ParseInput($command, [ref]$tokens, [ref]$errors)
        if ($errors.Count) { throw "The elevated worker command must parse before execution." }
        $match = [regex]::Match($command, 'FromBase64String\("([A-Za-z0-9+/=]+)"\)')
        if (-not $match.Success -or -not $command.Contains('Invoke-MxMscWorker') -or -not $command.Contains('IsInRole')) {
          throw "Elevated command must use encoded data, recheck elevation and avoid duplicate interactive confirmation."
        }
        $script:lastElevation = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($match.Groups[1].Value)) | ConvertFrom-Json
        if ($script:elevationFault -eq "cancel") { throw [ComponentModel.Win32Exception]::new(1223) }
        $diagnostic = "JNI error returned by system: 433 (recovered elevated fixture)"
        [IO.File]::AppendAllText($script:lastElevation.Operation.LogPath, $diagnostic + "`r`n")
        $console = "Completing the USB transition."
        if ($script:lastElevation.Operation.ShowDiagnostics) { $console += "`r`nVERBOSE: $diagnostic" }
        [IO.File]::WriteAllText($script:lastElevation.Log, $console)
        @{
          Success = $script:elevationFault -ne "worker"
          Error = $(if ($script:elevationFault -eq "worker") { "USB recovery timed out." } else { "" })
          Stage = "USB verification"
          LastState = "USB PID 3748, Windows status OK"
        } | ConvertTo-Json | Set-Content -LiteralPath $script:lastElevation.Result -Encoding utf8
        $process = [pscustomobject]@{ ExitCode=$(if ($script:elevationFault -eq "worker") { 1 } else { 0 }) }
        $process | Add-Member ScriptMethod WaitForExit {}
        $process | Add-Member ScriptMethod Dispose {}
        return $process
      }
      function script:Get-MxStLink {
        param($SerialNumber)
        [pscustomobject]@{
          SerialNumber=$SerialNumber
          Status="OK"
          ChildrenHealthy=$true
          DebugPresent=$true
          SerialPresent=$true
          UsbPid=$(if ($script:lastElevation.State -eq "Enabled") { "374B" } else { "3752" })
          MassStoragePresent=($script:lastElevation.State -eq "Enabled") -xor ($script:elevationFault -eq "state")
        }
      }
    }
    try {
      $quotedDirectory = Join-Path $fixture "vendor path with ' quotes"
      $quotedJava = Join-Path $fixture "java path with ' quotes\java.exe"
      foreach ($wanted in @("Enabled","Disabled")) {
        $operation = New-MxMscOperation -State $wanted -SerialNumber $serial -RootDirectory (Join-Path $fixture "diagnostic logs") -ShowDiagnostics:($wanted -eq "Disabled")
        $observed = @{ Probe=$null }
        $console = (& { $observed.Probe = Invoke-MxElevatedMsc -State $wanted -SerialNumber $serial -ToolDirectory $quotedDirectory -JavaPath $quotedJava -Operation $operation } *>&1 | Out-String)
        Assert (Test-MxMscState $observed.Probe $wanted) "elevated result is independently checked"
        $request = & $module { $script:lastElevation }
        Assert ($request.Module -ceq (Join-Path $PSScriptRoot "BoardMaintenance.psm1") -and $request.ToolDirectory -ceq $quotedDirectory -and
                $request.JavaPath -ceq $quotedJava -and $request.SerialNumber -ceq $serial) "UAC handoff preserves exact paths, quotes and probe identity"
        Assert ($console.Contains("JNI error") -eq $operation.ShowDiagnostics) "UAC output displays recovered diagnostics only in verbose mode"
        Assert ([IO.File]::ReadAllText($operation.LogPath).Contains("JNI error")) "elevated raw diagnostics persist in the operation log"
        Assert ((Get-Acl -LiteralPath (Split-Path -Parent $operation.LogPath)).AreAccessRulesProtected) "operation log directory has a private ACL"
        Assert (-not (Test-Path -LiteralPath (Split-Path -Parent $request.Log))) "successful elevation handoff cleans only its temporary private directory"
      }
      foreach ($fault in @("cancel","worker","state")) {
        & $module { param($Fault) $script:elevationFault=$Fault } $fault
        $expected = switch ($fault) { cancel { "Elevation was cancelled" }; worker { "elevated ST-Link operation failed" }; state { "USB state is not verified" } }
        $operation = New-MxMscOperation -State Disabled -SerialNumber $serial -RootDirectory (Join-Path $fixture "diagnostic logs")
        Reject { Invoke-MxElevatedMsc -State Disabled -SerialNumber $serial -ToolDirectory $quotedDirectory -JavaPath $quotedJava -Operation $operation } "UAC $fault is explicit" $expected
        $request = & $module { $script:lastElevation }
        Assert (Test-Path -LiteralPath $operation.LogPath) "failed elevated operations retain diagnostics"
        if ($fault -eq "worker") {
          Assert ($operation.Stage -ceq "USB verification" -and $operation.LastState.Contains("3748")) "terminal elevated stage and state reach the parent"
        }
        Assert (-not (Test-Path -LiteralPath (Split-Path -Parent $request.Log))) "failed elevation handoff leaves no temporary payload"
      }
      Write-Output "PASS: mocked UAC, safe arguments, quiet/verbose output, persistent private diagnostics, failure stage/state and temporary cleanup."
    } finally {
      & $module {
        param($Original)
        Remove-Item Function:script:Start-Process
        Set-Item Function:script:Get-MxStLink -Value $Original
      } $originalProbe
    }
    $originalOperationFactory = & $module { (Get-Item Function:New-MxMscOperation).ScriptBlock }
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
      $loaded = Microsoft.PowerShell.Core\Import-Module -Name $Name -DisableNameChecking:$DisableNameChecking -PassThru
      if (-not [object]::ReferenceEquals($loaded, $module) -or
          (Microsoft.PowerShell.Core\Get-Command Invoke-MxTool).ScriptBlock.ToString() -cne $mockInvoker -or
          (Microsoft.PowerShell.Core\Get-Command Get-MxStLink).ScriptBlock.ToString() -cne $mockProbe -or
          (Microsoft.PowerShell.Core\Get-Command Get-MxStLinkTransitionDevice).ScriptBlock.ToString() -cne $mockTransition -or
          (Microsoft.PowerShell.Core\Get-Command Invoke-MxElevatedMsc).ScriptBlock.ToString() -cne $mockElevation -or
          (Microsoft.PowerShell.Core\Get-Command New-MxMscOperation).ScriptBlock.ToString() -cne $mockOperationFactory) {
        throw "Test isolation failed; refusing to run an entry point with real hardware helpers."
      }
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
      function script:Get-MxStLinkTransitionDevice { throw "Unexpected transition query during image tests." }
      function script:Invoke-MxElevatedMsc { throw "Unexpected elevation during image tests." }
      function script:New-MxMscOperation { throw "Unexpected MSC operation log during image tests." }
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
          if ($script:fakeFault -eq "recovery") { return [pscustomobject]@{ ExitCode=1; Text="recovery fixture error" } }
          return [pscustomobject]@{ ExitCode=0; Text="MXCHIP_RECOVERY_OK`n" }
        }
        [IO.File]::WriteAllText((Join-Path $WorkingDirectory "state.txt"), "running 00000000")
        [IO.File]::WriteAllBytes((Join-Path $WorkingDirectory "prefix-a.bin"), $script:fakePrefix)
        $second = [byte[]]$script:fakePrefix.Clone()
        if ($script:fakeFault -eq "mismatch") { $second[100] = $second[100] -bxor 1 }
        [IO.File]::WriteAllBytes((Join-Path $WorkingDirectory "prefix-b.bin"), $second)
        [IO.File]::WriteAllBytes((Join-Path $WorkingDirectory "target-uid.bin"), [byte[]](1..12))
        if ($script:fakeFault -in @("process","recovery")) { return [pscustomobject]@{ ExitCode=1; Text="fixture failure" } }
        if ($script:fakeFault -eq "ack") { return [pscustomobject]@{ ExitCode=0; Text="no acknowledgement" } }
        return [pscustomobject]@{ ExitCode=0; Text="MXCHIP_SNAPSHOT_OK`n" }
      }
    } $prefix $app
    $mockInvoker = & $module { (Get-Item Function:Invoke-MxTool).ScriptBlock.ToString() }
    $mockProbe = & $module { (Get-Item Function:Get-MxStLink).ScriptBlock.ToString() }
    $mockTransition = & $module { (Get-Item Function:Get-MxStLinkTransitionDevice).ScriptBlock.ToString() }
    $mockElevation = & $module { (Get-Item Function:Invoke-MxElevatedMsc).ScriptBlock.ToString() }
    $mockOperationFactory = & $module { (Get-Item Function:New-MxMscOperation).ScriptBlock.ToString() }
    $scriptPath = Join-Path $PSScriptRoot "build-board-image.ps1"
    $output = Join-Path $fixture "success"
    $imageMessages = @(& $scriptPath -Application $appFile -OutputDirectory $output -Confirm:$false *>&1)
    Assert (($imageMessages[-1].ToString()).StartsWith("Verified: same-board image")) "board-image success is the last message after cleanup and diagnostics"
    $files = @(Get-ChildItem -LiteralPath $output -File)
    Assert ($files.Count -eq 3 -and (Test-Path -LiteralPath (Join-Path $output "operation.log"))) "image, manifest and private diagnostic log remain; raw dumps removed"
    $record = Get-Content -LiteralPath (Join-Path $output "manifest.json") -Raw | ConvertFrom-Json
    Assert ($record.confidential -and $record.prefixLength -eq 49152 -and $record.imageLength -eq $image.Length) "confidential provenance metadata"
    Assert ((Get-FileHash -LiteralPath (Join-Path $output $record.imageFile)).Hash -eq (Get-MxHash $image)) "full workflow publishes exact combined bytes"
    Assert ((Get-Acl -LiteralPath $output).AreAccessRulesProtected) "output ACL inheritance is disabled"
    foreach ($fault in @("mismatch", "process", "ack", "recovery")) {
      & $module { param($Fault) $script:fakeFault = $Fault } $fault
      $failedOutput = Join-Path $fixture $fault
      $messages = @()
      $caught = $null
      try { $messages = @(& $scriptPath -Application $appFile -OutputDirectory $failedOutput -Confirm:$false *>&1) } catch { $caught = $_ }
      Assert ($null -ne $caught -and $caught.Exception.Message.Contains("Diagnostics:")) "workflow rejects $fault with diagnostic location"
      Assert (@(Get-ChildItem -LiteralPath $failedOutput -File).Count -eq 1 -and
              (Test-Path -LiteralPath (Join-Path $failedOutput "operation.log"))) "failed workflow retains only private diagnostics, no image or dumps"
      $log = [IO.File]::ReadAllText((Join-Path $failedOutput "operation.log"))
      if ($fault -in @("process","ack")) { Assert ($log.Contains("MXCHIP_RECOVERY_OK")) "target state restoration is recorded but does not turn a failed snapshot into success" }
      if ($fault -eq "recovery") { Assert ($caught.Exception.Message.Contains("recovery failed")) "failed correction is explicit" }
    }
    $preview = Join-Path $fixture "preview"
    $before = & $module { $script:fakeCalls }
    & $scriptPath -Application $appFile -OutputDirectory $preview -WhatIf | Out-Null
    Assert (-not (Test-Path -LiteralPath $preview) -and (& $module { $script:fakeCalls }) -eq $before) "WhatIf never invokes OpenOCD or creates output"
    Reject { & $scriptPath -Application $appFile -OutputDirectory $output -Confirm:$false } "existing output never overwritten"
    Write-Output "PASS: private image workflow with simulated OpenOCD, target recovery, errors, cleanup and WhatIf."

    & $module {
      param($Factory,$LogRoot)
      $script:operationFactory = $Factory
      $script:operationRoot = $LogRoot
      $script:operations = @()
      $script:lastOperation = $null
      function script:New-MxMscOperation {
        param($State,$SerialNumber,[switch]$ShowDiagnostics)
        $script:lastOperation = & $script:operationFactory -State $State -SerialNumber $SerialNumber -ShowDiagnostics:$ShowDiagnostics -RootDirectory $script:operationRoot
        $script:operations += $script:lastOperation
        if ($script:mscFault -eq "log-write") { [IO.File]::SetAttributes($script:lastOperation.LogPath, [IO.FileAttributes]::ReadOnly) }
        return $script:lastOperation
      }
      $script:mscCalls = @()
      $script:mscExecutables = @()
      $script:mscFault = ""
      $script:mscPid = "374B"
      $script:mscDesired = "374B"
      $script:location = "fixture-usb-location"
      $script:vendorCalls = 0
      $script:restartCalls = 0
      $script:elevationCalls = 0
      $script:javaVersion = "25"
      $script:javaBits = "64"
      $script:administrator = $false
      $script:probeBusy = $false
      function script:Get-CimInstance {
        [CmdletBinding()]
        param([Parameter(Position=0)]$ClassName,$Filter)
        if ($ClassName -cne "Win32_Process") { throw "Unexpected process inventory request." }
        if ($script:probeBusy) {
          [pscustomobject]@{ Name="java.exe"; ProcessId=12345; CommandLine="java -jar STLinkUpgrade.jar" }
        }
      }
      function script:Test-MxAdministrator {
        return $script:administrator
      }
      function script:Get-MxStLinkTransitionDevice {
        param($SerialNumber)
        if ($SerialNumber -cne "066CFF515254667867131724") { throw "Unexpected probe selection." }
        if ($script:mscPid -eq "absent") { return $null }
        [pscustomobject]@{
          InstanceId="USB\VID_0483&PID_$script:mscPid\$SerialNumber"
          UsbPid=$script:mscPid
          Status="OK"
          LocationPath=$script:location
        }
      }
      function script:Get-MxStLink {
        param($SerialNumber)
        if ($script:mscPid -eq "3748") { throw "ST-Link is enumerated as PID 3748 in USB loader mode." }
        [pscustomobject]@{
          SerialNumber="066CFF515254667867131724"
          UsbPid=$script:mscPid
          InstanceId="USB\VID_0483&PID_$script:mscPid\066CFF515254667867131724"
          Status="OK"
          ChildrenHealthy=$true
          DebugPresent=$true
          SerialPresent=$true
          SerialPortNames=@($(if ($script:mscPid -eq "374B") { "USB Serial Device (COM8)" } else { "USB Serial Device (COM14)" }))
          MassStoragePresent=$script:mscPid -eq "374B"
        }
      }
      function script:Invoke-MxElevatedMsc {
        param($State,$SerialNumber,$ToolDirectory,$JavaPath,$Operation)
        $script:elevationCalls++
        $Operation.Stage = "Windows authorization"
        if (-not [IO.Path]::IsPathFullyQualified($ToolDirectory) -or -not [IO.Path]::IsPathFullyQualified($JavaPath) -or
            $SerialNumber -cne "066CFF515254667867131724") { throw "Elevated operation must retain resolved paths and the selected serial." }
        if ($script:mscFault -eq "uac") { throw "Elevation was cancelled; no updater launched." }
        $script:mscPid = if ($State -eq "Enabled") { "374B" } else { "3752" }
        return Get-MxStLink -SerialNumber $SerialNumber
      }
      function script:Test-MxUpdaterPackage {
        param($Directory)
        if ($script:mscFault -eq "package-changed" -and $script:vendorCalls -gt 0) {
          return [pscustomobject]@{ Jar="fixture.jar"; JarSha256="changed"; DriverSha256="DEF"; SignerSha256="fixture" }
        }
        [pscustomobject]@{ Jar="fixture.jar"; JarSha256="ABC"; DriverSha256="DEF"; SignerSha256="fixture" }
      }
      function script:Invoke-MxTool {
        param($FilePath,$Arguments,$WorkingDirectory,$TimeoutSeconds,[switch]$NeverKill)
        $script:mscCalls += ,$Arguments
        $script:mscExecutables += $FilePath
        if ($Arguments -contains "-checkParam") { throw "Parameter checks must not start a separate USB-loader transition." }
        if ($Arguments[0] -eq "/restart-device") {
          if ($FilePath -cne (Join-Path $env:SystemRoot "System32\pnputil.exe") -or $Arguments.Count -ne 2 -or
              $Arguments[1] -cne "USB\VID_0483&PID_$script:mscPid\066CFF515254667867131724") {
            throw "Only the currently observed exact ST-Link instance can be restarted."
          }
          $script:restartCalls++
          if ($script:mscFault -eq "restart-fails") { return [pscustomobject]@{ ExitCode=0; Text="Access is denied." } }
          $script:mscPid = if ($script:mscPid -eq "3748") { $script:mscDesired } else { "3748" }
          if ($script:mscFault -eq "stale-instance") {
            return [pscustomobject]@{ ExitCode=0; Text="Failed to restart device: The device is not connected." }
          }
          return [pscustomobject]@{ ExitCode=0; Text="Device restarted successfully." }
        }
        if (($Arguments -join ' ') -ceq "-XshowSettings:properties -version") {
          if ($NeverKill) { throw "Runtime version checks are bounded, non-device operations." }
          if ($script:mscFault -eq "java") { return [pscustomobject]@{ ExitCode=1; Text="Java fixture failure" } }
          return [pscustomobject]@{ ExitCode=0; Text="java.specification.version = $script:javaVersion`nsun.arch.data.model = $script:javaBits`n" }
        }
        if ($Arguments -notcontains "-force_prog" -or -not $NeverKill -or $Arguments -contains "mscAlwaysOff") { throw "Invalid mock updater operation" }
        $script:vendorCalls++
        $script:mscDesired = if ($Arguments -contains "mscOnOpt") { "374B" } else { "3752" }
        if ($script:mscFault -in @("entry","both","entry-twice","entry-ready","stale-instance","package-changed","moved") -and
            ($script:vendorCalls -eq 1 -or $script:mscFault -eq "entry-twice")) {
          if ($script:mscFault -in @("entry-ready","package-changed","moved")) { $script:mscPid = "3748" }
          if ($script:mscFault -eq "moved") { $script:location = "different-usb-port" }
          return [pscustomobject]@{ ExitCode=-1; Text="JNI error returned by system: 433`nError after GoToUsbLoader command." }
        }
        if ($script:mscFault -in @("exit","both")) {
          $script:mscPid = "3748"
          return [pscustomobject]@{ ExitCode=-1; Text="Firmware version detected: V2J48M35`n..........................Upgrade is successful.`nFailure exiting upgrade mode (Error 1)." }
        }
        if ($script:mscFault -eq "updater") { return [pscustomobject]@{ ExitCode=1; Text="firmware update failed" } }
        if ($script:mscFault -eq "uncertain") { return [pscustomobject]@{ ExitCode=-1; Text="Firmware version detected: V2J48M35`n.....`nError after GoToUsbLoader command." } }
        if ($script:mscFault -eq "missing-success") { return [pscustomobject]@{ ExitCode=0; Text="No explicit programming result." } }
        $script:mscPid = $script:mscDesired
        return [pscustomobject]@{ ExitCode=0; Text="Upgrade is successful." }
      }
    } $originalOperationFactory (Join-Path $fixture "workflow logs")
    $mockInvoker = & $module { (Get-Item Function:Invoke-MxTool).ScriptBlock.ToString() }
    $mockProbe = & $module { (Get-Item Function:Get-MxStLink).ScriptBlock.ToString() }
    $mockTransition = & $module { (Get-Item Function:Get-MxStLinkTransitionDevice).ScriptBlock.ToString() }
    $mockElevation = & $module { (Get-Item Function:Invoke-MxElevatedMsc).ScriptBlock.ToString() }
    $mockOperationFactory = & $module { (Get-Item Function:New-MxMscOperation).ScriptBlock.ToString() }
    $mscScript = Join-Path $PSScriptRoot "stlink-mass-storage.ps1"
    $javaFixture = Join-Path $PSHOME "pwsh.exe"
    $switchPreview = (& $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -WhatIf 3>&1 | Out-String)
    Assert ($switchPreview.Contains("does not invoke OpenOCD") -and $switchPreview -notmatch 'OpenOCD 0\.10') "MSC notice explains the vendor updater without claiming an outdated OpenOCD is active"
    Assert ((& $module { $script:mscCalls.Count }) -eq 0) "MSC WhatIf never loads the vendor tool"
    & $mscScript -Action Status -ToolDirectory $fixture -Confirm:$false | Out-Null
    Assert ((& $module { $script:mscCalls.Count }) -eq 0) "status needs no installed vendor package"
    Assert ((& $module { $script:elevationCalls }) -eq 0) "status and WhatIf do not request elevation"
    foreach ($requestedState in @("Enabled","Disabled")) {
      & $module { param($State) $script:mscPid = if ($State -eq "Enabled") { "374B" } else { "3752" } } $requestedState
      & $mscScript -Action $requestedState -ToolDirectory $fixture -Confirm:$false | Out-Null
      Assert ((& $module { $script:elevationCalls }) -eq 0) "already-correct $requestedState state needs no elevation"
    }
    Assert ((& $module { $script:operations.Count }) -eq 0) "status, WhatIf and no-op do not create operation logs"
    & $module { $script:mscPid = "374B" }
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "missing updater explains the setup step" "ST-Link updater is not prepared:.*STLinkUpgrade\.jar.*-Action Setup.*-ToolDirectory"
    [IO.File]::WriteAllText((Join-Path $fixture "STLinkUpgrade.jar"), "test fixture, never executed")
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "incomplete updater reports its missing native driver" "ST-Link updater is not prepared:.*STLinkUSBDriver\.dll"
    Assert ((& $module { $script:mscCalls.Count }) -eq 0 -and (& $module { $script:mscPid }) -eq "374B" -and (& $module { $script:elevationCalls }) -eq 0) "missing prerequisites do not prompt for UAC or launch Java"
    $null = New-Item -ItemType Directory -Path (Join-Path $fixture "native\win_x64") -Force
    [IO.File]::WriteAllText((Join-Path $fixture "native\win_x64\STLinkUSBDriver.dll"), "test fixture, never executed")
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath (Join-Path $fixture "missing-java.exe") -Confirm:$false } "invalid Java path is rejected before launching the updater" "Java executable not found"
    & $module { $script:mscFault = "uac" }
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "declining elevation causes an explicit failure without an updater" "Elevation was cancelled"
    Assert ((& $module { $script:mscCalls.Count }) -eq 0 -and (& $module { $script:mscPid }) -eq "374B") "UAC cancellation leaves the board unchanged"
    & $module { $script:mscFault = "" }
    & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false | Out-Null
    Assert ((& $module { $script:elevationCalls }) -eq 2 -and (& $module { $script:mscPid }) -eq "3752" -and (& $module { $script:mscCalls.Count }) -eq 0) "non-elevated switch delegates exactly once to the selected elevated action"
    & $module { $script:administrator = $true; $script:mscPid = "374B" }
    function Get-Command {
      [CmdletBinding()]
      param([Parameter(Position=0, Mandatory)][string]$Name, [string]$CommandType)
      if ($Name -cne "java" -or $CommandType -cne "Application") { throw "Unexpected Java discovery request." }
      [pscustomobject]@{ Source=$javaFixture }
      [pscustomobject]@{ Source=(Join-Path $fixture "second-java-must-not-be-selected.exe") }
    }
    & $mscScript -Action Disabled -ToolDirectory $fixture -Confirm:$false | Out-Null
    Assert ((& $module { $script:mscExecutables.Count }) -eq 2 -and @((& $module { $script:mscExecutables }) | Where-Object { $_ -cne $javaFixture }).Count -eq 0) "multiple Java installations select only the first executable on PATH"
    $calls = & $module { ,$script:mscCalls }
    Assert (($calls[0] -join ' ') -ceq "-XshowSettings:properties -version" -and $calls[1] -contains "--enable-native-access=ALL-UNNAMED" -and $calls[1] -contains "-force_prog") "Java 25 gets explicit JNI permission; exactly one vendor programming invocation"
    Remove-Item Function:Get-Command
    Assert ((& $module { $script:mscPid }) -eq "3752") "disable workflow changes the simulated BOARD personality"
    & $module { $script:javaVersion = "1.8" }
    & $mscScript -Action Enabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false | Out-Null
    Assert ((& $module { $script:mscPid }) -eq "374B") "reenable workflow does not depend on OpenOCD/PID 374B"
    $calls = & $module { ,$script:mscCalls }
    Assert ($calls[-1] -notcontains "--enable-native-access=ALL-UNNAMED") "Java 8 does not receive an unsupported modern JVM option"
    $count = & $module { $script:mscCalls.Count }
    & $mscScript -Action Enabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false | Out-Null
    Assert ((& $module { $script:mscCalls.Count }) -eq $count) "already-correct state needs no firmware write"
    foreach ($version in @("17", "24")) {
      & $module { param($Version) $script:javaVersion = $Version; $script:mscPid = "374B" } $version
      & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false | Out-Null
      $calls = & $module { ,$script:mscCalls }
      Assert (($calls[-1] -contains "--enable-native-access=ALL-UNNAMED") -eq ([int]$version -ge 24)) "native-access option starts at Java 24"
    }
    & $module { $script:mscPid = "374B"; $script:javaVersion = "25"; $script:javaBits = "32" }
    $count = & $module { $script:mscCalls.Count }
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "32-bit Java rejected without invoking updater" "requires a verified 64-bit"
    Assert ((& $module { $script:mscCalls.Count }) -eq $count + 1) "only the JVM inspection ran for incompatible Java"
    & $module { $script:javaBits = "64"; $script:javaVersion = "unknown" }
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "unknown runtime version rejected" "Could not identify"
    & $module { $script:javaVersion = "25" }
    foreach ($fault in @("java", "updater", "uncertain", "missing-success", "log-write")) {
      & $module { param($Fault) $script:mscFault = $Fault; $script:mscPid = "374B"; $script:vendorCalls = 0; $script:restartCalls = 0 } $fault
      $count = & $module { $script:mscCalls.Count }
      $expectedError = if ($fault -eq "java") { "Java runtime inspection failed" } elseif ($fault -eq "log-write") { "Diagnostic logging also failed" } else { "No automatic reflash" }
      Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "vendor $fault failure surfaced" $expectedError
      $expectedCalls = if ($fault -eq "log-write") { 0 } elseif ($fault -eq "java") { 1 } else { 2 }
      Assert ((& $module { $script:mscCalls.Count }) -eq $count + $expectedCalls) "failure stops without another vendor call"
      Assert ((& $module { $script:restartCalls }) -eq 0) "unknown programming outcomes do not trigger USB recovery"
      $operation = & $module { $script:lastOperation }
      $record = [IO.File]::ReadAllText($operation.LogPath)
      Assert (($fault -eq "log-write" -or $record.Contains("TERMINAL ERROR")) -and $operation.Stage -ne "Complete") "terminal failure is logged or logging failure is explicit, never a successful completion"
    }
    & $module { $script:mscFault=""; $script:mscPid="374B"; $script:vendorCalls=0; $script:probeBusy=$true }
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "another updater blocks a new vendor operation" "possible probe updater/debugger"
    Assert ((& $module { $script:vendorCalls }) -eq 0) "no vendor process starts alongside a conflicting updater"
    & $module { $script:mscPid="3748"; $script:restartCalls=0 }
    $operation = & $module { $script:lastOperation }
    Reject { Wait-MxStLinkMode -SerialNumber $serial -Mode Disabled -LocationPath "fixture-usb-location" -Operation $operation -RestartIfNeeded -TimeoutSeconds 5 } "active updater blocks USB restart" "possible probe updater/debugger"
    Assert ((& $module { $script:restartCalls }) -eq 0) "a device being programmed is not restarted"
    & $module { $script:probeBusy=$false }
    foreach ($fault in @("entry","entry-ready","exit","both","stale-instance")) {
      & $module { param($Fault) $script:mscFault=$Fault; $script:mscPid="374B"; $script:vendorCalls=0; $script:restartCalls=0 } $fault
      $records = @(& $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false *>&1)
      $messages = $records | Out-String
      $warnings = @($records | Where-Object { $_ -is [Management.Automation.WarningRecord] })
      Assert ($warnings.Count -eq 1 -and $warnings[0].Message.StartsWith("This reprograms")) "only the initial firmware-risk warning is shown"
      Assert ($messages -notmatch 'JNI error|GoToUsbLoader|Failure exiting upgrade mode|Failed to restart device|Windows restart exit|Programming has not started') "recovered vendor and Windows errors stay out of normal output"
      Assert ($messages.Contains("Verified: mass storage disabled.") -and $messages.Contains("COM14") -and $messages.Contains("Diagnostics:")) "success is concise, verified, and points to diagnostics"
      $textRecords = @($records | Where-Object { $_ -isnot [Management.Automation.WarningRecord] })
      Assert ($textRecords[0].ToString().StartsWith("Diagnostics:") -and $textRecords[-1].ToString().StartsWith("Verified:")) "diagnostics starts the operation and verified success ends it"
      $operation = & $module { $script:lastOperation }
      $record = [IO.File]::ReadAllText($operation.LogPath)
      Assert ($record.Contains("Vendor exit") -and $record.Contains("Verified result")) "complete diagnostics persist after successful recovery"
      if ($fault -ne "exit") { Assert ($record.Contains("GoToUsbLoader")) "recovered entry error is retained in the log" }
      if ($fault -in @("exit","both")) { Assert ($record.Contains("Failure exiting upgrade mode")) "recovered exit error is retained in the log" }
      if ($fault -eq "stale-instance") { Assert ($record.Contains("Failed to restart device")) "PnP restart error is retained even when re-enumeration succeeds" }
      $expectedVendor = if ($fault -eq "exit") { 1 } else { 2 }
      $expectedRestarts = if ($fault -eq "entry-ready") { 0 } elseif ($fault -eq "both") { 2 } else { 1 }
      Assert ((& $module { $script:mscPid }) -eq "3752" -and (& $module { $script:vendorCalls }) -eq $expectedVendor -and
              (& $module { $script:restartCalls }) -eq $expectedRestarts) "bounded, verified recovery for $fault without replaying a completed flash"
    }
    & $module { $script:mscFault="entry-ready"; $script:mscPid="374B"; $script:vendorCalls=0; $script:restartCalls=0 }
    $verboseRecords = @(& $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false -Verbose *>&1)
    Assert (($verboseRecords | Out-String).Contains("JNI error") -and @($verboseRecords | Where-Object { $_ -is [Management.Automation.VerboseRecord] }).Count -gt 0) "Verbose explicitly exposes raw recovered diagnostics"
    foreach ($fault in @("entry-twice","package-changed","moved")) {
      & $module { param($Fault) $script:mscFault=$Fault; $script:mscPid="374B"; $script:vendorCalls=0; $script:restartCalls=0; $script:location="fixture-usb-location" } $fault
      $expectedError = switch ($fault) { entry-twice { "No automatic reflash" }; package-changed { "Vendor package changed" }; moved { "physical location changed" } }
      Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "unsafe continuation $fault rejected" $expectedError
      Assert ($Error[0].Exception.Message.Contains("Stage:") -and $Error[0].Exception.Message.Contains("Last observed state:") -and
              $Error[0].Exception.Message.Contains("Diagnostics:")) "terminal failures identify stage, last state and retained log"
      $maximumVendor = if ($fault -eq "entry-twice") { 2 } else { 1 }
      Assert ((& $module { $script:vendorCalls }) -eq $maximumVendor) "no uncontrolled vendor retries after $fault"
    }
    & $module { $script:mscPid="3748"; $script:location="fixture-usb-location"; $script:restartCalls=0 }
    $count = & $module { $script:mscCalls.Count }
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "an unrelated initial loader state is never treated as a pending operation" "PID 3748"
    Assert ((& $module { $script:mscCalls.Count }) -eq $count) "initial loader rejection starts no updater"
    & $module { $script:mscPid="absent" }
    $operation = & $module { $script:lastOperation }
    Reject { Wait-MxStLinkMode -SerialNumber $serial -Mode Disabled -LocationPath "fixture-usb-location" -Operation $operation -RestartIfNeeded -TimeoutSeconds 1 } "missing device times out explicitly" "did not reach verified"
    & $module { $script:mscPid="3748"; $script:mscFault="restart-fails"; $script:restartCalls=0 }
    Reject { Wait-MxStLinkMode -SerialNumber $serial -Mode Disabled -LocationPath "fixture-usb-location" -Operation $operation -RestartIfNeeded -TimeoutSeconds 8 } "PnPUtil zero exit is not success when interfaces do not return" "did not reach verified"
    Assert ([IO.File]::ReadAllText($operation.LogPath).Contains("Access is denied")) "failed Windows recovery details remain in diagnostics"
    Assert ((& $module { $script:restartCalls }) -eq 2) "at most two exact-device restart requests per transition"
    $mockInvoker = "deliberately invalid mock identity"
    $callsBefore = & $module { $script:mscCalls.Count }
    Reject { & $mscScript -Action Disabled -ToolDirectory $fixture -JavaPath $javaFixture -Confirm:$false } "entry-point guard blocks stale or replaced mocks" "Test isolation failed"
    Assert ((& $module { $script:mscCalls.Count }) -eq $callsBefore) "isolation failure cannot reach the process launcher"
    Write-Output "PASS: quiet recovered output, retained private logs, Verbose/UAC propagation, terminal stage/state/log, logging failure, safe recovery and unchanged retry limits."
  }
} finally {
  if (Test-Path Function:Import-Module) { Remove-Item Function:Import-Module }
  if (Test-Path Function:Get-Command) { Remove-Item Function:Get-Command }
  Remove-Module $module -Force
  Remove-Item -LiteralPath $fixture -Recurse -Force
}
Write-Output "PASS: exact image/host-sector preservation, snapshot command safety, reversible MSC options, USB-state verification and signed-manifest parsing."
