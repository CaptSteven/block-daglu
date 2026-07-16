param(
  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",
  [switch]$NoToolchain,
  [switch]$CompetitionToolchain
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repoRoot "build\\blockdaglu"

Write-Host "Configuring..."
$bundledTool = Join-Path $repoRoot "third_party\SparseMatrixLUTestTool\tool\include\ALInterface.h"
$WithToolchain = $CompetitionToolchain -or ((-not $NoToolchain) -and (Test-Path $bundledTool))

$extra = @()
if ($WithToolchain) {
  $extra += "-DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=ON"
  $extra += "-DBLOCKDAGLU_BUILD_SMOKETEST=ON"
} else {
  $extra += "-DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=OFF"
  $extra += "-DBLOCKDAGLU_BUILD_SMOKETEST=OFF"
}

if (![string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
  $toolchainFile = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
  if (Test-Path $toolchainFile) {
    $extra += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
  }
}

& cmake -S (Join-Path $repoRoot "blockdaglu") -B $buildDir -DCMAKE_BUILD_TYPE=$Config @extra
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

Write-Host "Building ($Config)..."
& cmake --build $buildDir --config $Config -j
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

Write-Host "Done: $buildDir"
