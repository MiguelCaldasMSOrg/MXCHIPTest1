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
  if (-not $Root) {
    $cli = (Get-Command arduino-cli -CommandType Application -ErrorAction Stop).Source
    $result = Invoke-MxTool $cli @("config", "get", "directories.data", "--json") (Get-Location).Path
    Assert-MxNativeSuccess $result "Arduino CLI configuration"
    $dataDirectory = $result.Text | ConvertFrom-Json
    if ($dataDirectory -isnot [string] -or -not [IO.Path]::IsPathFullyQualified($dataDirectory)) {
      throw "Arduino CLI did not return an absolute data directory for OpenOCD discovery."
    }
    $Root = Join-Path $dataDirectory "packages\AZ3166\tools\openocd\0.10.0"
  }
  $rootPath = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path
  $exe = Join-Path $rootPath "bin\openocd.exe"
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
    throw "The no-MSC probe (PID 3752) needs newer OpenOCD. Supply -OpenOcdRoot (0.11+ with HLA) or reenable MSC; core 0.10 uses the wrong endpoints."
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
    $manifestDigest = [Convert]::ToBase64String([Security.Cryptography.SHA256]::HashData($manifest))
    if ($sfAttributes[0]["SHA-256-Digest-Manifest"] -cne $manifestDigest) { throw "Signed JAR manifest digest mismatch." }
    $sections = @(ConvertFrom-MxJarAttributes ([Text.Encoding]::UTF8.GetString($manifest)))
    if ($sections[0]["Main-Class"] -ne "com.st.stlinkupgrade.app.MainApp" -or $sections[0]["Class-Path"] -ne ".") {
      throw "Unexpected updater entry point or external class path."
    }
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($section in $sections | Select-Object -Skip 1) {
      $name = $section["Name"]
      if (-not $name -or -not $names.Add($name)) { throw "Duplicate/missing signed JAR member name." }
      $bytes = Read-MxZipEntry $zip $name
      if ($section["SHA-256-Digest"] -cne [Convert]::ToBase64String([Security.Cryptography.SHA256]::HashData($bytes))) {
        throw "Updater member hash mismatch: $name"
      }
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
    foreach ($token in @("-msvcp", "-dynOpt", "mscOffOpt", "mscOnOpt", "-force_prog", "-checkParam", "-sn", "-list")) {
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

function New-MxMscArguments {
  param([Parameter(Mandatory)][string]$SerialNumber, [ValidateSet("Enabled", "Disabled")][string]$State, [switch]$Preflight)
  if ($SerialNumber -notmatch '^[0-9A-Fa-f]{24}$') { throw "Invalid ST-Link serial number." }
  $option = if ($State -eq "Enabled") { "mscOnOpt" } else { "mscOffOpt" }
  $operation = if ($Preflight) { "-checkParam" } else { "-force_prog" }
  return @("-sn", $SerialNumber.ToUpperInvariant(), "-msvcp", "-dynOpt", $option, $operation)
}

function Test-MxMscState {
  param([Parameter(Mandatory)]$Probe, [ValidateSet("Enabled", "Disabled")][string]$State)
  $wanted = $State -eq "Enabled"
  return $Probe.Status -eq "OK" -and $Probe.ChildrenHealthy -and $Probe.DebugPresent -and $Probe.SerialPresent -and ($Probe.MassStoragePresent -eq $wanted)
}

Export-ModuleMember -Function Get-MxHash, Assert-MxImage, Join-MxBoardImage, Invoke-MxTool, Assert-MxNativeSuccess,
  Get-MxStLink, Assert-MxLocalDirectory, New-MxPrivateDirectory, Resolve-MxOpenOcd,
  New-MxSnapshotConfiguration, New-MxSnapshotRecoveryConfiguration, Read-MxZipEntry, ConvertFrom-MxJarAttributes,
  Test-MxUpdaterPackage, New-MxMscArguments, Test-MxMscState
