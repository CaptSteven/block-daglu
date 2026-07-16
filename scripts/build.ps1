param(
  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",
  [switch]$NoToolchain
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repoRoot "build\\blockdaglu"

Write-Host "Configuring..."
$WithToolchain = -not $NoToolchain

$extra = @()
if ($WithToolchain) {
  $extra += "-DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=ON"
  $extra += "-DBLOCKDAGLU_BUILD_SMOKETEST=ON"
} else {
  $extra += "-DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=OFF"
  $extra += "-DBLOCKDAGLU_BUILD_SMOKETEST=OFF"
}

& cmake -S (Join-Path $repoRoot "blockdaglu") -B $buildDir -DCMAKE_BUILD_TYPE=$Config @extra

Write-Host "Building ($Config)..."
& cmake --build $buildDir --config $Config -j

Write-Host "Done: $buildDir"
