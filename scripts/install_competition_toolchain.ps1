param(
  [string]$ArchivePath = "",
  [string]$Url = "",
  [string]$Sha256 = "",
  [string]$Token = "",
  [switch]$Force
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$destination = Join-Path $repoRoot "third_party\SparseMatrixLUTestTool"
$downloadDir = Join-Path $repoRoot ".cache\competition-toolchain"
$extractDir = Join-Path $downloadDir "extract"

if ([string]::IsNullOrWhiteSpace($Url)) { $Url = $env:BLOCKDAGLU_TOOLCHAIN_URL }
if ([string]::IsNullOrWhiteSpace($Sha256)) { $Sha256 = $env:BLOCKDAGLU_TOOLCHAIN_SHA256 }
if ([string]::IsNullOrWhiteSpace($Token)) { $Token = $env:BLOCKDAGLU_TOOLCHAIN_TOKEN }
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
  if ([string]::IsNullOrWhiteSpace($Url)) {
    throw "Provide -ArchivePath or set BLOCKDAGLU_TOOLCHAIN_URL."
  }
  $ArchivePath = Join-Path $downloadDir "SparseMatrixLUTestTool.zip"
  $headers = @{}
  if (![string]::IsNullOrWhiteSpace($Token)) { $headers["Authorization"] = "Bearer $Token" }
  Invoke-WebRequest -Uri $Url -Headers $headers -OutFile $ArchivePath
}

$ArchivePath = (Resolve-Path $ArchivePath).Path
if ([string]::IsNullOrWhiteSpace($Sha256)) {
  throw "Provide -Sha256 or set BLOCKDAGLU_TOOLCHAIN_SHA256."
}
$actualHash = (Get-FileHash -Algorithm SHA256 -Path $ArchivePath).Hash.ToLowerInvariant()
if ($actualHash -ne $Sha256.Trim().ToLowerInvariant()) {
  throw "Competition toolchain SHA-256 mismatch."
}

if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
Expand-Archive -Path $ArchivePath -DestinationPath $extractDir -Force

$candidate = $extractDir
$header = Get-ChildItem -Path $extractDir -Filter ALInterface.h -File -Recurse | Select-Object -First 1
if ($null -eq $header -or $header.Directory.Name -ne "include") {
  throw "Archive does not contain tool/include/ALInterface.h."
}
$candidate = Split-Path (Split-Path $header.Directory.FullName -Parent) -Parent
if (!(Test-Path (Join-Path $candidate "tool\include\ALInterface.h"))) {
  throw "Unable to identify SparseMatrixLUTestTool archive root."
}

if (Test-Path $destination) {
  if (!$Force) { throw "Destination exists: $destination. Re-run with -Force to replace it." }
  Remove-Item -Recurse -Force $destination
}
New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
Copy-Item -Recurse -Path $candidate -Destination $destination
Write-Host "Competition toolchain installed and verified: $actualHash"
