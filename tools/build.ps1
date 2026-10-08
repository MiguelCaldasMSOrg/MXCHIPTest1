param(
  [ValidateRange(0, 15)][int]$Mode,
  [string]$BuildDirectory
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ($BuildDirectory) {
  if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
  } else {
    [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
  }
} else {
  Join-Path $projectRoot "build"
}
$buildSketchPath = Join-Path (Join-Path $projectRoot "_build") "MXCHIPTest1"
$generatedSketchPath = Join-Path $buildPath "sketch"
$databasePath = Join-Path $buildPath "compile_commands.json"
$sketchPath = Join-Path $projectRoot "MXCHIPTest1.ino"
$sourcePath = Join-Path $projectRoot "src"
$libraryPath = Join-Path $projectRoot "libraries"
$separator = [System.IO.Path]::DirectorySeparatorChar
$normalBuild = Join-Path $projectRoot "build"
$scratchRoot = (Join-Path $projectRoot "_build") + $separator
if (($buildPath -ne $normalBuild -and -not $buildPath.StartsWith($scratchRoot, [StringComparison]::OrdinalIgnoreCase)) -or
    $buildPath -eq $buildSketchPath -or $buildPath.StartsWith($buildSketchPath + $separator, [StringComparison]::OrdinalIgnoreCase)) {
  throw "BuildDirectory must be build or a separate child of _build, never the staged sketch directory."
}

function Get-ProjectSources {
  @(Get-Item -LiteralPath $sketchPath) + @(Get-ChildItem -LiteralPath $sourcePath -Recurse -File) + @(Get-ChildItem -LiteralPath $libraryPath -Recurse -File)
}

Push-Location $projectRoot
try {
  & (Join-Path $PSScriptRoot "verify-stselib.ps1")
  $obsoleteFullImage = Join-Path $buildPath "MXCHIPTest1.full.bin"
  if (Test-Path -LiteralPath $obsoleteFullImage) {
    $obsolete = Get-Item -LiteralPath $obsoleteFullImage
    if ($obsolete.PSIsContainer -or ($obsolete.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
      throw "Refusing to remove an unexpected full-image path."
    }
    Remove-Item -LiteralPath $obsolete.FullName
    Write-Output "Removed obsolete generated full image; builds are application-only."
  }
  $sourceFiles = @(Get-ProjectSources)
  if (Test-Path -LiteralPath $buildSketchPath) {
    $staging = Get-Item -LiteralPath $buildSketchPath
    if ($staging.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
      throw "Refusing to clear a staging directory that is a link."
    }
    # This is the generated sketch copy, not the project's source tree.
    Remove-Item -LiteralPath $staging.FullName -Recurse -Force
  }
  New-Item -ItemType Directory -Path $buildSketchPath -Force | Out-Null
  $sourceHashes = @{}
  foreach ($file in $sourceFiles) {
    $relative = $file.FullName.Substring($projectRoot.Length + 1)
    $destination = Join-Path $buildSketchPath $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
    $sourceHashes[$relative] = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash
  }

  # The staged sketch deliberately excludes local upload-port metadata.
  $modeArguments = @()
  if ($PSBoundParameters.ContainsKey("Mode")) {
    $modeArguments = @("--build-property", "compiler.cpp.extra_flags=-DMXCHIP_TEST_MODE=$Mode")
  }
  & arduino-cli compile --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 --libraries (Join-Path $buildSketchPath "libraries") --build-path $buildPath --output-dir $buildPath @modeArguments $buildSketchPath
  if ($LASTEXITCODE -ne 0) {
    throw "Arduino compilation failed with exit code $LASTEXITCODE."
  }
  $currentSources = @(Get-ProjectSources)
  if ($currentSources.Count -ne $sourceHashes.Count) {
    throw "The source file list changed during compilation; rebuild before uploading."
  }
  foreach ($file in $currentSources) {
    $relative = $file.FullName.Substring($projectRoot.Length + 1)
    if (-not $sourceHashes.ContainsKey($relative) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash -ne $sourceHashes[$relative]) {
      throw "Source changed during compilation: $relative. Rebuild before uploading."
    }
  }

  & (Join-Path $PSScriptRoot "validate-firmware.ps1") -ApplicationPath (Join-Path $buildPath "MXCHIPTest1.ino.bin")
  & (Join-Path $PSScriptRoot "verify-legacy-link.ps1") -BuildDirectory $buildPath

  $database = ConvertFrom-Json -InputObject (Get-Content -LiteralPath $databasePath -Raw)
  $database = @($database)
  $aliases = @()
  foreach ($entry in $database) {
    $compiledFile = [System.IO.Path]::GetFullPath($entry.file)
    $original = $null
    $isSketch = [System.IO.Path]::GetFileName($compiledFile) -eq "MXCHIPTest1.ino.cpp"
    if ($isSketch) {
      $original = $sketchPath
    } else {
      foreach ($root in @($generatedSketchPath, $buildSketchPath)) {
        $prefix = $root + $separator
        if ($compiledFile.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
          $relative = $compiledFile.Substring($prefix.Length)
          if ($sourceHashes.ContainsKey($relative)) {
            $original = Join-Path $projectRoot $relative
          }
          break
        }
      }
    }
    if (-not $original) {
      continue
    }

    $alias = $entry | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $alias.file = $original
    $alias.directory = $projectRoot
    $alias.arguments = @($entry.arguments | ForEach-Object {
      if ($_ -eq $entry.file) {
        if ($isSketch) {
          "-x"
          "c++"
          "-include"
          "Arduino.h"
        }
        $original
      } else {
        $argument = $_
        foreach ($root in @($generatedSketchPath, $buildSketchPath)) {
          $prefix = "-I" + $root
          if ($argument.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase) -and
              ($argument.Length -eq $prefix.Length -or $argument[$prefix.Length] -in @([char]'\', [char]'/'))) {
            $argument = "-I" + $projectRoot + $argument.Substring($prefix.Length)
            break
          }
        }
        $argument
      }
    })
    $aliases += $alias
  }
  foreach ($file in $sourceFiles | Where-Object { $_.Extension -in @(".ino", ".cpp", ".cc", ".cxx", ".c", ".s") }) {
    if (@($aliases | Where-Object { $_.file -eq $file.FullName }).Count -ne 1) {
      throw "Expected one IntelliSense entry for $($file.FullName)."
    }
  }
  ConvertTo-Json -InputObject @($database + $aliases) -Depth 20 |
    Set-Content -LiteralPath $databasePath -Encoding utf8
  Write-Output "IntelliSense configured for $($aliases.Count) project translation units."
} finally {
  Pop-Location
}
