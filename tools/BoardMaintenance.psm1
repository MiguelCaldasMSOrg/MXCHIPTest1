Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-MxHash {
  param([Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes)
  [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes))
}

function Assert-MxImage {
  param(
    [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes,
    [uint32]$Address = 0x0800C000,
    [int]$MaximumSize = (0x100000 - 0xC000)
  )
  if ($Bytes.Length -lt 8) {
    throw "The application image is missing its vector table."
  }
  if ($Bytes.Length -gt $MaximumSize) {
    throw "The application exceeds the AZ3166's flash capacity after offset 0xC000."
  }
  $stack = [BitConverter]::ToUInt32($Bytes, 0)
  $reset = [BitConverter]::ToUInt32($Bytes, 4)
  if ($stack -le 0x20000000 -or $stack -gt 0x20040000 -or $stack % 8 -ne 0) {
    throw "The application stack pointer is outside the expected aligned AZ3166 RAM range."
  }
  if ($reset % 2 -ne 1) {
    throw "The application reset vector must select Thumb mode."
  }
  if (($reset - 1) -lt $Address -or ($reset - 1) -ge ([long]$Address + $Bytes.Length)) {
    throw "The application reset vector is outside the expected application flash region."
  }
}

function Join-MxBoardImage {
  param(
    [Parameter(Mandatory)][byte[]]$Prefix,
    [Parameter(Mandatory)][byte[]]$SecondRead,
    [Parameter(Mandatory)][byte[]]$Application
  )
  if ($Prefix.Length -ne 0xC000 -or $SecondRead.Length -ne 0xC000) {
    throw "Both board-prefix reads must contain exactly 49152 bytes."
  }
  if ((Get-MxHash $Prefix) -ne (Get-MxHash $SecondRead)) {
    throw "Board-prefix reads differ; no image can be published."
  }
  Assert-MxImage -Bytes $Prefix -Address 0x08000000 -MaximumSize 0xC000
  Assert-MxImage -Bytes $Application
  $image = [byte[]]::new(0xC000 + $Application.Length)
  [Array]::Copy($Prefix, 0, $image, 0, $Prefix.Length)
  [Array]::Copy($Application, 0, $image, 0xC000, $Application.Length)
  return ,$image
}

function Invoke-MxTool {
  param(
    [Parameter(Mandatory)][string]$FilePath,
    [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Arguments,
    [Parameter(Mandatory)][string]$WorkingDirectory,
    [int]$TimeoutSeconds = 60,
    [switch]$NeverKill
  )
  $info = [Diagnostics.ProcessStartInfo]::new()
  $info.FileName = $FilePath
  $info.WorkingDirectory = $WorkingDirectory
  $info.UseShellExecute = $false
  $info.RedirectStandardOutput = $true
  $info.RedirectStandardError = $true
  foreach ($argument in $Arguments) {
    $info.ArgumentList.Add($argument)
  }
  $process = [Diagnostics.Process]::new()
  $process.StartInfo = $info
  try {
    if (-not $process.Start()) { throw "Could not start $FilePath." }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if ($NeverKill) {
      # Interrupting a firmware updater is unsafe; let the vendor process finish.
      $process.WaitForExit()
    } elseif (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
      $process.Kill($true)
      $process.WaitForExit()
      throw "$FilePath timed out. Device state must be checked before retrying."
    }
    $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    [pscustomobject]@{ ExitCode = $process.ExitCode; Text = $text }
  } finally {
    $process.Dispose()
  }
}

function Assert-MxNativeSuccess {
  param([Parameter(Mandatory)]$Result, [Parameter(Mandatory)][string]$Operation)
  if ($Result.ExitCode -ne 0) {
    throw "$Operation failed (exit $($Result.ExitCode)).`n$($Result.Text)"
  }
}

function Get-MxStLink {
  param([string]$SerialNumber)
  if (-not $IsWindows) { throw "These maintenance entry points require Windows and its PnpDevice module." }
  if ($SerialNumber -and $SerialNumber -notmatch '^[0-9A-Fa-f]{24}$') {
    throw "ST-Link serial number must be exactly 24 hexadecimal characters."
  }
  $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
  $probes = @($devices | Where-Object { $_.InstanceId -match '^USB\\VID_0483&PID_(374B|3752)\\[0-9A-Fa-f]{24}$' })
  if ($SerialNumber) {
    $probes = @($probes | Where-Object { $_.InstanceId.EndsWith("\$SerialNumber", [StringComparison]::OrdinalIgnoreCase) })
  }
  if ($probes.Count -eq 0) {
    $other = @($devices | Where-Object { $_.InstanceId -match '^USB\\VID_0483&PID_3748\\[0-9A-Fa-f]{24}$' })
    if ($SerialNumber) { $other = @($other | Where-Object { $_.InstanceId.EndsWith("\$SerialNumber", [StringComparison]::OrdinalIgnoreCase) }) }
    if ($other.Count -gt 0) {
      throw "ST-Link is enumerated as PID 3748, not a supported V2-1 application interface (374B/3752). After an updater operation this can be USB loader mode. A switch must start from a verified application identity; automatic loader continuation is allowed only within that same operation. No update was started."
    }
  }
  if ($probes.Count -ne 1) {
    throw "Expected exactly one selected ST-Link/V2-1; found $($probes.Count). Connect it or specify -SerialNumber."
  }
  $probe = $probes[0]
  $null = $probe.InstanceId -match '^USB\\VID_0483&PID_([0-9A-Fa-f]{4})\\([0-9A-Fa-f]{24})$'
  $pidText = $Matches[1].ToUpperInvariant()
  $serial = $Matches[2].ToUpperInvariant()
  $children = @()
  foreach ($candidate in $devices | Where-Object { $_.InstanceId -match "^USB\\VID_0483&PID_$pidText&MI_" }) {
    $parent = Get-PnpDeviceProperty -InstanceId $candidate.InstanceId -KeyName DEVPKEY_Device_Parent -ErrorAction Stop
    if ($parent.Data -eq $probe.InstanceId) {
      $compatible = Get-PnpDeviceProperty -InstanceId $candidate.InstanceId -KeyName DEVPKEY_Device_CompatibleIds -ErrorAction Stop
      $children += [pscustomobject]@{
        InstanceId = $candidate.InstanceId
        Status = $candidate.Status
        Class = $candidate.Class
        FriendlyName = $candidate.FriendlyName
        CompatibleIds = @($compatible.Data)
      }
    }
  }
  $storage = @($children | Where-Object { $_.CompatibleIds -match '^USB\\Class_08' })
  $ports = @($children | Where-Object { $_.Class -eq "Ports" })
  $debug = @($children | Where-Object { $_.CompatibleIds -match '^USB\\Class_FF' })
  [pscustomobject]@{
    SerialNumber = $serial
    UsbPid = $pidText
    InstanceId = $probe.InstanceId
    Status = $probe.Status
    MassStoragePresent = $storage.Count -gt 0
    SerialPresent = $ports.Count -gt 0
    SerialPortNames = @($ports | ForEach-Object FriendlyName)
    DebugPresent = $debug.Count -gt 0
    ChildrenHealthy = @($children | Where-Object { $_.Status -ne "OK" }).Count -eq 0
  }
}

function Assert-MxLocalDirectory {
  param([Parameter(Mandatory)][string]$Path, [string]$ForbiddenRoot)
  if (-not [IO.Path]::IsPathFullyQualified($Path) -or $Path.StartsWith('\\') -or $Path -notmatch '^[A-Za-z]:\\') {
    throw "Choose an absolute local Windows path, not a network share or relative path."
  }
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
  if ($full.Length -le 3) { throw "A drive root is not a valid output directory." }
  if ($ForbiddenRoot) {
    $root = [IO.Path]::GetFullPath($ForbiddenRoot).TrimEnd('\')
    if ($full -eq $root -or $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
      throw "Secret-bearing board images must be outside the repository."
    }
  }
  $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($full))
  if ($drive.DriveType -ne [IO.DriveType]::Fixed -or $drive.DriveFormat -notin @("NTFS", "ReFS")) {
    throw "Output must be on a fixed local NTFS/ReFS volume, never the AZ3166 virtual disk or removable media."
  }
  $current = $full
  while ($current) {
    if (Test-Path -LiteralPath $current) {
      $item = Get-Item -LiteralPath $current -Force
      if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Output path contains a file, junction, or symbolic link: $current"
      }
      if ($ForbiddenRoot -and (Test-Path -LiteralPath (Join-Path $current ".git"))) {
        throw "Confidential output must not be inside any Git worktree."
      }
    }
    $next = [IO.Path]::GetDirectoryName($current)
    if ($next -eq $current) { break }
    $current = $next
  }
  return $full
}

function New-MxPrivateDirectory {
  param([Parameter(Mandatory)][string]$Path)
  if (Test-Path -LiteralPath $Path) { throw "Refusing to reuse or overwrite an existing output directory: $Path" }
  $null = New-Item -ItemType Directory -Path $Path -ErrorAction Stop
  $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
  $acl = [Security.AccessControl.DirectorySecurity]::new()
  $acl.SetOwner($sid)
  $acl.SetAccessRuleProtection($true, $false)
  $rule = [Security.AccessControl.FileSystemAccessRule]::new($sid, "FullControl", "ContainerInherit,ObjectInherit", "None", "Allow")
  $acl.AddAccessRule($rule)
  Set-Acl -LiteralPath $Path -AclObject $acl -ErrorAction Stop
  $actual = Get-Acl -LiteralPath $Path
  $rules = @($actual.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier]))
  if (-not $actual.AreAccessRulesProtected -or $rules.Count -ne 1 -or $rules[0].IdentityReference -ne $sid) {
    throw "Could not restrict the output directory to the current Windows user; no flash data will be read."
  }
}

function Resolve-MxOpenOcd {
  param([string]$Root, [Parameter(Mandatory)][string]$UsbPid)
  $exe = $null
  if (-not $Root) {
    $cli = (Get-Command arduino-cli -CommandType Application -ErrorAction Stop).Source
    $result = Invoke-MxTool $cli @("board", "details", "--fqbn", "AZ3166:stm32f4:MXCHIP_AZ3166", "--show-properties=expanded") (Get-Location).Path
    Assert-MxNativeSuccess $result "Arduino OpenOCD configuration"
    $paths = @([regex]::Matches($result.Text, '(?m)^tools\.openocd\.path=([^\r\n]+)'))
    $commands = @([regex]::Matches($result.Text, '(?m)^tools\.openocd\.cmd=([^\r\n]+)'))
    if ($paths.Count -ne 1 -or $commands.Count -ne 1 -or -not [IO.Path]::IsPathFullyQualified($paths[0].Groups[1].Value)) {
      throw "Arduino did not provide an unambiguous absolute OpenOCD tool path and command. Check platform.local.txt or specify -OpenOcdRoot."
    }
    $exe = [IO.Path]::GetFullPath((Join-Path $paths[0].Groups[1].Value ($commands[0].Groups[1].Value.Replace('/', '\'))))
    $Root = Split-Path -Parent (Split-Path -Parent $exe)
  }
  $rootPath = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path
  if (-not $exe) { $exe = Join-Path $rootPath "bin\openocd.exe" }
  if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing OpenOCD component: $exe" }
  $scripts = $null
  $interfaceScript = $null
  foreach ($relative in @("scripts", "openocd\scripts", "share\openocd\scripts")) {
    $candidate = Join-Path $rootPath $relative
    if (-not (Test-Path -LiteralPath (Join-Path $candidate "target\stm32f4x.cfg") -PathType Leaf)) { continue }
    foreach ($interface in @("interface\stlink-hla.cfg", "interface\stlink-v2-1.cfg")) {
      if (Test-Path -LiteralPath (Join-Path $candidate $interface) -PathType Leaf) {
        $scripts = $candidate
        $interfaceScript = $interface
        break
      }
    }
    if ($scripts) { break }
  }
  if (-not $scripts) { throw "Missing OpenOCD Tcl scripts; expected scripts, openocd\scripts, or share\openocd\scripts under $rootPath." }
  $version = Invoke-MxTool $exe @("--version") $rootPath
  Assert-MxNativeSuccess $version "OpenOCD version"
  if ($version.Text -notmatch 'Open On-Chip Debugger\s+(\d+)\.(\d+)') { throw "Unrecognized OpenOCD version." }
  if ($UsbPid -eq "3752" -and [int]$Matches[1] -eq 0 -and [int]$Matches[2] -lt 11) {
    throw "The selected OpenOCD executable '$exe' reports $($version.Text.Trim()) and cannot handle the no-MSC USB endpoints. Configure a compatible installation or supply -OpenOcdRoot (0.11+ with HLA)."
  }
  $configuration = Invoke-MxTool $exe @("-s", $scripts, "-f", $interfaceScript, "-c", "transport select hla_swd", "-f", "target\stm32f4x.cfg", "-c", "echo MXCHIP_HLA_CONFIG_OK", "-c", "shutdown") $rootPath -TimeoutSeconds 15
  Assert-MxNativeSuccess $configuration "OpenOCD HLA configuration (no target initialization)"
  if ($configuration.Text -notmatch '(?m)^MXCHIP_HLA_CONFIG_OK\s*$') {
    throw "OpenOCD did not confirm the HLA interface/transport configuration. No target was initialized."
  }
  [pscustomobject]@{ Exe = $exe; Scripts = $scripts; InterfaceScript = $interfaceScript; Version = $version.Text.Trim() }
}

function New-MxSnapshotConfiguration {
  param(
    [Parameter(Mandatory)][string]$SerialNumber,
    [Parameter(Mandatory)][string]$UsbPid,
    [ValidateSet("interface\stlink-hla.cfg", "interface\stlink-v2-1.cfg")][string]$InterfaceScript = "interface\stlink-v2-1.cfg"
  )
  if ($SerialNumber -notmatch '^[0-9A-Fa-f]{24}$' -or $UsbPid -notin @("374B", "3752")) {
    throw "Invalid ST-Link selection."
  }
  $preamble = @"
gdb_port disabled
tcl_port disabled
telnet_port disabled
source [find {$InterfaceScript}]
hla_vid_pid 0x0483 0x$UsbPid
hla_serial $SerialNumber
transport select hla_swd
source [find {target\stm32f4x.cfg}]
"@
  # Override automatic debug-register writes, then save/restore only the watchdog-freeze register.
  $body = @'
$_TARGETNAME configure -event examine-end {}
set previous unknown
set have_watchdog 0
set result [catch {
    init
    poll
    if {[expr [mrw 0xE0042000] & 0xFFF] != 0x441} { error "Expected STM32F412 device ID 0x441" }
    mem2array capacity 16 0x1FFF7A22 1
    if {$capacity(0) != 1024} { error "Expected 1024 KiB STM32 flash" }
    set options [mrw 0x40023C14]
    if {[expr ($options >> 8) & 0xFF] != 0xAA} { error "RDP is enabled; never unlock to obtain a backup" }
    if {[expr $options & 0x80000000] != 0} { error "PCROP mode is enabled; backup refused" }
    set previous [$_TARGETNAME curstate]
    if {$previous != "running" && $previous != "halted"} { error "Target must be running or halted" }
    set watchdog [mrw 0xE0042008]
    set statefile [open state.txt w]
    puts $statefile "$previous [format %08x $watchdog]"
    close $statefile
    set have_watchdog 1
    mww 0xE0042008 [expr $watchdog | 0x00001800]
    if {$previous == "running"} { halt 5000 }
    dump_image prefix-a.bin 0x08000000 0xC000
    dump_image prefix-b.bin 0x08000000 0xC000
    dump_image target-uid.bin 0x1FFF7A10 12
} reason]
set cleanup [catch {
    if {$previous == "running"} { resume }
    if {$have_watchdog} { mww 0xE0042008 $watchdog }
} cleanup_reason]
if {$cleanup != 0} {
    echo "MXCHIP_SNAPSHOT_CLEANUP_FAILED: $cleanup_reason"
    shutdown error
} elseif {$result != 0} {
    echo "MXCHIP_SNAPSHOT_FAILED: $reason"
    shutdown error
} else {
    echo "MXCHIP_SNAPSHOT_OK"
    shutdown
}
'@
  return $preamble + "`n" + $body
}

function New-MxSnapshotRecoveryConfiguration {
  param(
    [string]$SerialNumber,
    [string]$UsbPid,
    [Parameter(Mandatory)][string]$State,
    [ValidateSet("interface\stlink-hla.cfg", "interface\stlink-v2-1.cfg")][string]$InterfaceScript = "interface\stlink-v2-1.cfg"
  )
  if ($State.Trim() -notmatch '^(running|halted) ([0-9a-fA-F]{8})$') { throw "Unrecognized saved target state; manual recovery required." }
  $running = $Matches[1] -eq "running"
  $watchdog = $Matches[2]
  $preamble = (New-MxSnapshotConfiguration $SerialNumber $UsbPid -InterfaceScript $InterfaceScript).Split('$_TARGETNAME', [StringSplitOptions]::None)[0]
  $resume = if ($running) { "resume" } else { "" }
  return $preamble + "`n" + '$_TARGETNAME configure -event examine-end {}' + "`ninit`n$resume`nmww 0xE0042008 0x$watchdog`necho MXCHIP_RECOVERY_OK`nshutdown`n"
}

function Read-MxZipEntry {
  param([Parameter(Mandatory)]$Zip, [Parameter(Mandatory)][string]$Name)
  $entry = $Zip.GetEntry($Name)
  if ($null -eq $entry -or $entry.Length -gt 16MB) { throw "Missing or oversized archive entry: $Name" }
  $stream = $entry.Open()
  $memory = [IO.MemoryStream]::new()
  try {
    $stream.CopyTo($memory)
    return ,$memory.ToArray()
  } finally {
    $stream.Dispose()
    $memory.Dispose()
  }
}

function ConvertFrom-MxJarAttributes {
  param([Parameter(Mandatory)][string]$Text)
  $sections = $Text.Replace("`r`n", "`n") -replace "`n ", ""
  $result = @()
  foreach ($section in $sections -split "`n`n") {
    if (-not $section.Trim()) { continue }
    $attributes = @{}
    foreach ($line in $section -split "`n") {
      if (-not $line) { continue }
      if ($line -notmatch '^([^:]+): (.*)$' -or $attributes.ContainsKey($Matches[1])) {
        throw "Invalid or duplicate JAR manifest attribute."
      }
      $attributes[$Matches[1]] = $Matches[2]
    }
    $result += $attributes
  }
  return $result
}

function Assert-MxJarDigest {
  param(
    [Parameter(Mandatory)][System.Collections.IDictionary]$Attributes,
    [Parameter(Mandatory)][ValidateSet("Digest", "Digest-Manifest")][string]$Suffix,
    [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes,
    [Parameter(Mandatory)][string]$Description
  )
  $found = $false
  foreach ($algorithm in @("SHA-256", "SHA-384")) {
    $name = "$algorithm-$Suffix"
    if (-not $Attributes.Contains($name)) { continue }
    $found = $true
    $digest = if ($algorithm -eq "SHA-256") {
      [Security.Cryptography.SHA256]::HashData($Bytes)
    } else {
      [Security.Cryptography.SHA384]::HashData($Bytes)
    }
    if ($Attributes[$name] -cne [Convert]::ToBase64String($digest)) {
      throw "$Description $algorithm digest mismatch."
    }
  }
  if (-not $found) { throw "$Description has no supported SHA-256 or SHA-384 digest." }
}

function Test-MxUpdaterPackage {
  param([Parameter(Mandatory)][string]$Directory)
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  Add-Type -AssemblyName System.Security.Cryptography.Pkcs
  $jar = Join-Path $Directory "STLinkUpgrade.jar"
  $dll = Join-Path $Directory "native\win_x64\STLinkUSBDriver.dll"
  foreach ($file in @($jar, $dll)) {
    $item = Get-Item -LiteralPath $file -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "Updater components must be regular files: $file"
    }
  }
  $zip = [IO.Compression.ZipFile]::OpenRead($jar)
  try {
    $sf = Read-MxZipEntry $zip "META-INF/ST_PRIVA.SF"
    $rsa = Read-MxZipEntry $zip "META-INF/ST_PRIVA.RSA"
    $manifest = Read-MxZipEntry $zip "META-INF/MANIFEST.MF"
    $content = [Security.Cryptography.Pkcs.ContentInfo]::new($sf)
    $signature = [Security.Cryptography.Pkcs.SignedCms]::new($content, $true)
    $signature.Decode($rsa)
    $signature.CheckSignature($true)
    if ($signature.SignerInfos.Count -ne 1) { throw "Unexpected ST updater signer count." }
    $certificate = $signature.SignerInfos[0].Certificate
    # ST's private signing CA is not in normal OS roots; explicitly pin the inspected vendor signer.
    $signerHash = $certificate.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256)
    if ($signerHash -ne "7B35ED6E0BD638A2E4D376A4E7E2F4AF30294A1123127EB5FD7578F7B08A4B93" -or
        [DateTime]::UtcNow -lt $certificate.NotBefore.ToUniversalTime() -or [DateTime]::UtcNow -gt $certificate.NotAfter.ToUniversalTime()) {
      throw "Unrecognized or expired ST updater signer; review a new vendor package before updating the signer pin. No bypass is provided."
    }
    $sfAttributes = @(ConvertFrom-MxJarAttributes ([Text.Encoding]::UTF8.GetString($sf)))
    Assert-MxJarDigest $sfAttributes[0] "Digest-Manifest" $manifest "Signed JAR manifest"
    $sections = @(ConvertFrom-MxJarAttributes ([Text.Encoding]::UTF8.GetString($manifest)))
    if ($sections[0]["Main-Class"] -ne "com.st.stlinkupgrade.app.MainApp" -or $sections[0]["Class-Path"] -ne ".") {
      throw "Unexpected updater entry point or external class path."
    }
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($section in $sections | Select-Object -Skip 1) {
      $name = $section["Name"]
      if (-not $name -or -not $names.Add($name)) { throw "Duplicate/missing signed JAR member name." }
      $bytes = Read-MxZipEntry $zip $name
      Assert-MxJarDigest $section "Digest" $bytes "Updater member '$name'"
    }
    $allNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $tokens = ""
    foreach ($entry in $zip.Entries) {
      if (-not $allNames.Add($entry.FullName)) { throw "Duplicate archive entry." }
      if ($entry.FullName.EndsWith('/')) { continue }
      if ($entry.FullName -in @("META-INF/MANIFEST.MF", "META-INF/ST_PRIVA.SF", "META-INF/ST_PRIVA.RSA")) { continue }
      if (-not $names.Contains($entry.FullName)) { throw "Unsigned updater archive member: $($entry.FullName)" }
      if ($entry.FullName -like "*.class") {
        $tokens += [Text.Encoding]::ASCII.GetString((Read-MxZipEntry $zip $entry.FullName))
      }
    }
    foreach ($token in @("-msvcp", "-dynOpt", "mscOffOpt", "mscOnOpt", "-force_prog", "-sn", "-list")) {
      if (-not $tokens.Contains($token)) { throw "This vendor updater lacks the required CLI capability: $token" }
    }
  } finally {
    $zip.Dispose()
  }
  $nativeSignature = Get-AuthenticodeSignature -LiteralPath $dll
  if ($nativeSignature.Status -ne "Valid" -or $null -eq $nativeSignature.SignerCertificate -or
      $nativeSignature.SignerCertificate.Subject -notmatch 'O=STMicroelectronics International N\.V\.') {
    throw "The native ST-Link driver does not have a valid STMicroelectronics Authenticode signature."
  }
  [pscustomobject]@{
    Jar = $jar
    JarSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $jar).Hash
    DriverSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash
    SignerSha256 = $signerHash
  }
}

function Test-MxAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  try {
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  } finally {
    $identity.Dispose()
  }
}

function New-MxMscOperation {
  param(
    [Parameter(Mandatory)][ValidateSet("Enabled", "Disabled")][string]$State,
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$SerialNumber,
    [switch]$ShowDiagnostics,
    [string]$RootDirectory = (Join-Path $env:LOCALAPPDATA "MXCHIPTest1\Logs\STLink")
  )
  $name = "{0}-{1}-{2}" -f [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssfffZ"), $State.ToLowerInvariant(), [Guid]::NewGuid().ToString("N")
  $directory = Assert-MxLocalDirectory -Path (Join-Path $RootDirectory $name) -ForbiddenRoot (Split-Path -Parent $PSScriptRoot)
  New-MxPrivateDirectory $directory
  $operation = [pscustomobject]@{
    LogPath = Join-Path $directory "operation.log"
    ShowDiagnostics = [bool]$ShowDiagnostics
    Stage = "Preparation"
    LastState = "Not observed"
  }
  [IO.File]::WriteAllText($operation.LogPath, "ST-Link mass-storage operation`r`nRequested state: $State`r`nSerial: $SerialNumber`r`n", [Text.UTF8Encoding]::new($false))
  return $operation
}

function Write-MxMscDiagnostic {
  [CmdletBinding()]
  param([Parameter(Mandatory)]$Operation, [Parameter(Mandatory)][AllowEmptyString()][string]$Message)
  $entry = "[{0}] [{1}] {2}" -f [DateTime]::UtcNow.ToString("o"), $Operation.Stage, $Message
  [IO.File]::AppendAllText($Operation.LogPath, $entry + "`r`n", [Text.UTF8Encoding]::new($false))
  Write-Verbose -Message $Message -Verbose:$Operation.ShowDiagnostics
}

function Write-MxMscProgress {
  param([Parameter(Mandatory)]$Operation, [Parameter(Mandatory)][string]$Message)
  Write-MxMscDiagnostic $Operation "Progress: $Message"
  Write-Host $Message
}

function Invoke-MxElevatedMsc {
  param(
    [Parameter(Mandatory)][ValidateSet("Enabled", "Disabled")][string]$State,
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$SerialNumber,
    [Parameter(Mandatory)][string]$ToolDirectory,
    [Parameter(Mandatory)][string]$JavaPath,
    [Parameter(Mandatory)]$Operation
  )
  $temporary = Join-Path ([IO.Path]::GetTempPath()) ("mxchip-msc-" + [Guid]::NewGuid().ToString("N"))
  New-MxPrivateDirectory $temporary
  $log = Join-Path $temporary "operation.log"
  $resultPath = Join-Path $temporary "result.json"
  try {
    $request = @{
      Module = Join-Path $PSScriptRoot "BoardMaintenance.psm1"
      State = $State
      SerialNumber = $SerialNumber
      ToolDirectory = $ToolDirectory
      JavaPath = $JavaPath
      Log = $log
      Result = $resultPath
      Operation = $Operation
    } | ConvertTo-Json -Depth 4 -Compress
    $payload = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($request))
    $command = @'
$ErrorActionPreference = "Stop"
$request = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("__REQUEST__")) | ConvertFrom-Json
$operation = $request.Operation
$result = @{ Success=$false; Error=""; Stage=$operation.Stage; LastState=$operation.LastState }
try {
  [IO.File]::WriteAllText($request.Log, "")
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  try {
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw "Windows did not grant elevation; no updater was launched." }
  } finally {
    $identity.Dispose()
  }
  Import-Module $request.Module -Force -DisableNameChecking
  & {
    $null = Invoke-MxMscWorker -State $request.State -SerialNumber $request.SerialNumber -ToolDirectory $request.ToolDirectory -JavaPath $request.JavaPath -Operation $operation
    $result.Success = $true
  } *>&1 | Out-File -LiteralPath $request.Log -Append -Encoding utf8
} catch {
  $result.Error = $_.Exception.Message
  try {
    [IO.File]::AppendAllText($operation.LogPath, "TERMINAL WORKER ERROR: " + $_.Exception.ToString() + "`r`n")
  } catch {
    $result.Error += " Diagnostic logging also failed: " + $_.Exception.Message
  }
} finally {
  $result.Stage = $operation.Stage
  $result.LastState = $operation.LastState
  [IO.File]::WriteAllText($request.Result, ($result | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
}
if ($result.Success) { exit 0 }
exit 1
'@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command.Replace("__REQUEST__", $payload)))
    $Operation.Stage = "Windows authorization"
    Write-MxMscProgress $Operation "Waiting for Windows authorization..."
    $process = $null
    try {
      try {
        $process = Start-Process -FilePath (Join-Path $PSHOME "pwsh.exe") -ArgumentList @("-NoProfile", "-NonInteractive", "-EncodedCommand", $encoded) -Verb RunAs -PassThru -ErrorAction Stop
      } catch {
        throw "Elevation was cancelled or could not be started; no elevated updater was launched. $($_.Exception.Message)"
      }
      $process.WaitForExit()
      if (Test-Path -LiteralPath $log -PathType Leaf) {
        Write-Host ([IO.File]::ReadAllText($log).Trim())
      }
      if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "The elevated worker did not return its operation result (exit $($process.ExitCode)); no additional update was attempted."
      }
      $worker = [IO.File]::ReadAllText($resultPath) | ConvertFrom-Json
      $Operation.Stage = $worker.Stage
      $Operation.LastState = $worker.LastState
      if ($process.ExitCode -ne 0 -or -not $worker.Success) {
        throw "The elevated ST-Link operation failed (exit $($process.ExitCode)). $($worker.Error)"
      }
    } finally {
      if ($process) { $process.Dispose() }
    }
    $Operation.Stage = "Final USB verification"
    $probe = Get-MxStLink -SerialNumber $SerialNumber
    $Operation.LastState = "USB PID $($probe.UsbPid), Windows status $($probe.Status)"
    Write-MxMscDiagnostic $Operation ("Parent verification: " + ($probe | ConvertTo-Json -Compress))
    if (-not (Test-MxMscState $probe $State)) { throw "The elevated process returned, but the requested USB state is not verified." }
    return $probe
  } finally {
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log -Force }
    if (Test-Path -LiteralPath $resultPath -PathType Leaf) { Remove-Item -LiteralPath $resultPath -Force }
    Remove-Item -LiteralPath $temporary -Force
  }
}

function Get-MxStLinkTransitionDevice {
  param([Parameter(Mandatory)][ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$SerialNumber)
  $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object {
    $_.InstanceId -match "^USB\\VID_0483&PID_(374B|3752|3748)\\$SerialNumber$"
  })
  if ($devices.Count -gt 1) { throw "Multiple USB identities match the selected ST-Link; refusing to choose a device to restart." }
  if ($devices.Count -eq 0) { return $null }
  $device = $devices[0]
  $null = $device.InstanceId -match '^USB\\VID_0483&PID_([0-9A-Fa-f]{4})\\'
  $pidText = $Matches[1].ToUpperInvariant()
  $locations = @(Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName DEVPKEY_Device_LocationPaths -ErrorAction Stop | ForEach-Object Data)
  if ($locations.Count -eq 0 -or [string]::IsNullOrWhiteSpace($locations[0])) { throw "The selected ST-Link's physical USB location is unavailable." }
  [pscustomobject]@{
    InstanceId = $device.InstanceId
    UsbPid = $pidText
    Status = $device.Status
    LocationPath = [string]$locations[0]
  }
}

function Assert-MxNoProbeProcess {
  $processes = @(Get-CimInstance Win32_Process -Filter "Name='java.exe' OR Name='javaw.exe' OR Name='openocd.exe' OR Name='ST-LinkUpgrade.exe' OR Name='STLinkUpgrade.exe'" -ErrorAction Stop)
  $busy = @($processes | Where-Object {
    $_.Name -notin @("java.exe", "javaw.exe") -or [string]::IsNullOrWhiteSpace($_.CommandLine) -or $_.CommandLine -match 'STLinkUpgrade\.jar'
  })
  if ($busy.Count) {
    $names = ($busy | ForEach-Object { "$($_.Name) (PID $($_.ProcessId))" }) -join ", "
    throw "A possible probe updater/debugger is still running: $names. No USB restart or further updater launch is allowed."
  }
}

function Wait-MxStLinkMode {
  param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$SerialNumber,
    [Parameter(Mandatory)][ValidateSet("Loader", "Enabled", "Disabled")][string]$Mode,
    [Parameter(Mandatory)][string]$LocationPath,
    [Parameter(Mandatory)]$Operation,
    [switch]$RestartIfNeeded,
    [ValidateRange(1, 90)][int]$TimeoutSeconds = 45
  )
  $wantedPid = switch ($Mode) { Loader { "3748" }; Enabled { "374B" }; Disabled { "3752" } }
  $timer = [Diagnostics.Stopwatch]::StartNew()
  $stable = 0
  $restarts = 0
  $nextRestartMs = 3000
  $lastProblem = "The selected device is not enumerated."
  $lastRestart = ""
  $Operation.Stage = if ($Mode -eq "Loader") { "Entering firmware-update mode" } else { "USB verification ($Mode)" }
  while ($timer.ElapsedMilliseconds -lt $TimeoutSeconds * 1000) {
    $device = $null
    try {
      $device = Get-MxStLinkTransitionDevice -SerialNumber $SerialNumber
    } catch {
      $lastProblem = "USB enumeration: $($_.Exception.Message)"
      Write-MxMscDiagnostic $Operation $lastProblem
    }
    if ($device) {
      $Operation.LastState = "USB PID $($device.UsbPid), Windows status $($device.Status)"
      Write-MxMscDiagnostic $Operation ("Observed device: " + ($device | ConvertTo-Json -Compress))
      if ($device.InstanceId -ine "USB\VID_0483&PID_$($device.UsbPid)\$SerialNumber" -or $device.LocationPath -cne $LocationPath) {
        throw "The selected ST-Link's USB identity or physical location changed; no restart or further programming is allowed."
      }
      $lastProblem = "PID $($device.UsbPid), device status $($device.Status); waiting for $Mode."
      if ($device.UsbPid -eq $wantedPid -and $device.Status -eq "OK") {
        $ready = $true
        $probe = $device
        if ($Mode -ne "Loader") {
          try {
            $probe = Get-MxStLink -SerialNumber $SerialNumber
            $ready = Test-MxMscState $probe $Mode
          } catch {
            $ready = $false
            $lastProblem = "Application enumeration: $($_.Exception.Message)"
            Write-MxMscDiagnostic $Operation $lastProblem
          }
        }
        if ($ready) {
          $stable++
          if ($stable -ge 2) { return $probe }
        } else {
          $stable = 0
          $lastProblem = "The requested USB PID is present, but debug/VCP/MSC or driver health is not ready."
        }
      } else {
        $stable = 0
      }
      if ($RestartIfNeeded -and $stable -eq 0 -and $restarts -lt 2 -and $timer.ElapsedMilliseconds -ge $nextRestartMs) {
        if (-not (Test-MxAdministrator)) { throw "Elevation is required for the selected ST-Link USB restart." }
        Assert-MxNoProbeProcess
        $restarts++
        Write-MxMscProgress $Operation "Reconnecting ST-Link..."
        Write-MxMscDiagnostic $Operation "Restarting only $($device.InstanceId) (attempt $restarts/2)."
        $result = Invoke-MxTool (Join-Path $env:SystemRoot "System32\pnputil.exe") @("/restart-device", $device.InstanceId) $PSScriptRoot -TimeoutSeconds 20
        $lastRestart = "Windows restart exit $($result.ExitCode): $($result.Text.Trim())"
        Write-MxMscDiagnostic $Operation $lastRestart
        # PnPUtil can return zero on failure or race re-enumeration; only the observed state proves success.
        $nextRestartMs = $timer.ElapsedMilliseconds + 3000
      }
    } else {
      $stable = 0
      $Operation.LastState = "Selected USB device not currently enumerated"
    }
    Start-Sleep -Milliseconds 500
  }
  throw "ST-Link did not reach verified $Mode state within $TimeoutSeconds seconds. $lastProblem No further programming was attempted."
}

function Invoke-MxMscWorker {
  param(
    [Parameter(Mandatory)][ValidateSet("Enabled", "Disabled")][string]$State,
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$SerialNumber,
    [Parameter(Mandatory)][string]$ToolDirectory,
    [Parameter(Mandatory)][string]$JavaPath,
    [Parameter(Mandatory)]$Operation
  )
  if (-not (Test-MxAdministrator)) { throw "The ST-Link switching worker requires elevation." }
  $Operation.Stage = "Checking USB identity"
  $probe = Get-MxStLink -SerialNumber $SerialNumber
  $Operation.LastState = "USB PID $($probe.UsbPid), Windows status $($probe.Status)"
  Write-MxMscDiagnostic $Operation ("Initial device: " + ($probe | ConvertTo-Json -Compress))
  if (Test-MxMscState $probe $State) { return $probe }
  $Operation.Stage = "Validating updater"
  $package = Test-MxUpdaterPackage $ToolDirectory
  Write-MxMscDiagnostic $Operation ("Authenticated package: " + ($package | ConvertTo-Json -Compress))
  $Operation.Stage = "Inspecting Java runtime"
  $runtime = Invoke-MxTool $JavaPath @("-XshowSettings:properties", "-version") $ToolDirectory -TimeoutSeconds 15
  Write-MxMscDiagnostic $Operation "Java exit $($runtime.ExitCode):`r`n$($runtime.Text)"
  if ($runtime.ExitCode -ne 0) { throw "Java runtime inspection failed (exit $($runtime.ExitCode)); the updater was not launched." }
  if ($runtime.Text -notmatch '(?m)^\s*sun\.arch\.data\.model\s*=\s*64\s*$') {
    throw "The updater requires a verified 64-bit Java runtime. Select one with -JavaPath; the updater was not launched."
  }
  if ($runtime.Text -notmatch '(?m)^\s*java\.specification\.version\s*=\s*(?:1\.)?([0-9]+)\s*$') {
    throw "Could not identify the Java runtime version; the updater was not launched."
  }
  $javaArguments = @("-Djava.awt.headless=true")
  if ([int]$Matches[1] -ge 24) { $javaArguments += "--enable-native-access=ALL-UNNAMED" }
  $javaArguments += @("-jar", $package.Jar)
  $Operation.Stage = "Rechecking USB identity"
  $confirmed = Get-MxStLink -SerialNumber $SerialNumber
  if ($confirmed.UsbPid -ne $probe.UsbPid -or $confirmed.Status -ne "OK" -or -not $confirmed.ChildrenHealthy -or
      -not $confirmed.DebugPresent -or -not $confirmed.SerialPresent -or $confirmed.MassStoragePresent -ne $probe.MassStoragePresent) {
    throw "The selected probe changed or is not healthy; the updater was not launched."
  }
  return Invoke-MxMscChange -InitialProbe $confirmed -State $State -Package $package -JavaPath $JavaPath -JavaArguments $javaArguments -ToolDirectory $ToolDirectory -Operation $Operation
}

function Invoke-MxMscChange {
  param(
    [Parameter(Mandatory)]$InitialProbe,
    [Parameter(Mandatory)][ValidateSet("Enabled", "Disabled")][string]$State,
    [Parameter(Mandatory)]$Package,
    [Parameter(Mandatory)][string]$JavaPath,
    [Parameter(Mandatory)][string[]]$JavaArguments,
    [Parameter(Mandatory)][string]$ToolDirectory,
    [Parameter(Mandatory)]$Operation
  )
  $Operation.Stage = "Acquiring exclusive probe access"
  if (-not (Test-MxAdministrator)) { throw "The ST-Link switching worker requires elevation." }
  $serial = $InitialProbe.SerialNumber
  $arguments = @(New-MxMscArguments -SerialNumber $serial -State $State)
  $mutex = [Threading.Mutex]::new($false, "Global\MXCHIPTest1-STLink-$($serial.ToUpperInvariant())")
  $locked = $false
  try {
    try {
      $locked = $mutex.WaitOne(0)
    } catch [Threading.AbandonedMutexException] {
      $locked = $true
      throw "A previous operation on this ST-Link ended unexpectedly; inspect its state before starting another update."
    }
    if (-not $locked) { throw "Another scripted operation owns this ST-Link; no updater was launched." }
    return Invoke-MxMscChangeCore -InitialProbe $InitialProbe -State $State -Package $Package -JavaPath $JavaPath -JavaArguments $JavaArguments -ToolDirectory $ToolDirectory -Arguments $arguments -Operation $Operation
  } finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
  }
}

function Invoke-MxMscChangeCore {
  param(
    [Parameter(Mandatory)]$InitialProbe,
    [Parameter(Mandatory)][ValidateSet("Enabled", "Disabled")][string]$State,
    [Parameter(Mandatory)]$Package,
    [Parameter(Mandatory)][string]$JavaPath,
    [Parameter(Mandatory)][string[]]$JavaArguments,
    [Parameter(Mandatory)][string]$ToolDirectory,
    [Parameter(Mandatory)][string[]]$Arguments,
    [Parameter(Mandatory)]$Operation
  )
  $serial = $InitialProbe.SerialNumber
  $initialState = if ($State -eq "Enabled") { "Disabled" } else { "Enabled" }
  if (-not (Test-MxMscState $InitialProbe $initialState)) { throw "A switch must start from a healthy, identified V2-1 application state." }
  $context = Get-MxStLinkTransitionDevice -SerialNumber $serial
  if (-not $context -or $context.InstanceId -ine $InitialProbe.InstanceId -or $context.Status -ne "OK") {
    throw "The selected ST-Link changed before the update; no updater was started."
  }
  for ($attempt = 0; $attempt -lt 2; $attempt++) {
    $Operation.Stage = "Pre-update checks"
    Assert-MxNoProbeProcess
    $current = Get-MxStLinkTransitionDevice -SerialNumber $serial
    $expectedPid = if ($attempt -eq 0) { $InitialProbe.UsbPid } else { "3748" }
    if (-not $current -or $current.UsbPid -ne $expectedPid -or $current.Status -ne "OK" -or
        $current.InstanceId -ine "USB\VID_0483&PID_$expectedPid\$serial" -or $current.LocationPath -cne $context.LocationPath) {
      throw "The selected ST-Link changed before the vendor invocation; no further programming is allowed."
    }
    $Operation.LastState = "USB PID $($current.UsbPid), Windows status $($current.Status)"
    $verified = Test-MxUpdaterPackage $ToolDirectory
    if ($verified.JarSha256 -cne $Package.JarSha256 -or $verified.DriverSha256 -cne $Package.DriverSha256) {
      throw "Vendor package changed during the operation; no further programming is allowed."
    }
    $Operation.Stage = "Firmware update"
    Write-MxMscDiagnostic $Operation "Vendor invocation $($attempt + 1): $JavaPath $($JavaArguments -join ' ') $($Arguments -join ' ')"
    $result = Invoke-MxTool $JavaPath ($JavaArguments + $Arguments) $ToolDirectory -NeverKill
    Write-MxMscDiagnostic $Operation "Vendor exit $($result.ExitCode):`r`n$($result.Text)"
    if ($result.Text -match '(?m)^\s*\.*Upgrade is successful\.\s*$') {
      if ($result.ExitCode -ne 0 -and $result.Text -notmatch 'Failure exiting upgrade mode') {
        throw "Programming was reported successful, but an unexpected vendor error occurred (exit $($result.ExitCode)). No reflash is allowed."
      }
      Write-MxMscProgress $Operation "Verifying the requested USB state..."
      return Wait-MxStLinkMode -SerialNumber $serial -Mode $State -LocationPath $context.LocationPath -Operation $Operation -RestartIfNeeded
    }
    $openingFailure = $result.ExitCode -ne 0 -and
      $result.Text -match '(?m)^\s*(Unexpected error during opening \([0-9]+\)\.|Error (in|after) GoToUsbLoader command\.)' -and
      $result.Text -notmatch '(?im)Firmware version detected|Upgrade is|^\s*\.{3,}|programming|erasing|writing'
    if ($attempt -eq 0 -and $openingFailure) {
      Write-MxMscDiagnostic $Operation "Recognized pre-programming entry failure; one continuation is permitted after verifying the selected loader."
      Write-MxMscProgress $Operation "Completing the USB transition..."
      $null = Wait-MxStLinkMode -SerialNumber $serial -Mode Loader -LocationPath $context.LocationPath -Operation $Operation -RestartIfNeeded
      continue
    }
    throw "ST-Link update failed or its programming outcome is uncertain (exit $($result.ExitCode)). No automatic reflash is allowed."
  }
}

function New-MxMscArguments {
  param([Parameter(Mandatory)][string]$SerialNumber, [ValidateSet("Enabled", "Disabled")][string]$State)
  if ($SerialNumber -notmatch '^[0-9A-Fa-f]{24}$') { throw "Invalid ST-Link serial number." }
  $option = if ($State -eq "Enabled") { "mscOnOpt" } else { "mscOffOpt" }
  return @("-sn", $SerialNumber.ToUpperInvariant(), "-msvcp", "-dynOpt", $option, "-force_prog")
}

function Test-MxMscState {
  param([Parameter(Mandatory)]$Probe, [ValidateSet("Enabled", "Disabled")][string]$State)
  $wanted = $State -eq "Enabled"
  return $Probe.Status -eq "OK" -and $Probe.ChildrenHealthy -and $Probe.DebugPresent -and $Probe.SerialPresent -and ($Probe.MassStoragePresent -eq $wanted)
}

Export-ModuleMember -Function Get-MxHash, Assert-MxImage, Join-MxBoardImage, Invoke-MxTool, Assert-MxNativeSuccess,
  Get-MxStLink, Assert-MxLocalDirectory, New-MxPrivateDirectory, Resolve-MxOpenOcd,
  New-MxSnapshotConfiguration, New-MxSnapshotRecoveryConfiguration, Read-MxZipEntry, ConvertFrom-MxJarAttributes,
  Test-MxUpdaterPackage, Test-MxAdministrator, New-MxMscOperation, Write-MxMscDiagnostic,
  Write-MxMscProgress, Invoke-MxElevatedMsc, Get-MxStLinkTransitionDevice,
  Wait-MxStLinkMode, Invoke-MxMscWorker, Invoke-MxMscChange, New-MxMscArguments, Test-MxMscState
