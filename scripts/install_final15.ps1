param(
  [string]$ArchivePath = "",
  [string]$Url = "",
  [string]$Sha256 = "",
  [string]$Token = "",
  [string]$Destination = "",
  [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $repoRoot "data\FinalTestCases"
}
if ([string]::IsNullOrWhiteSpace($Url)) { $Url = $env:BLOCKDAGLU_DATASET_URL }
if ([string]::IsNullOrWhiteSpace($Sha256)) { $Sha256 = $env:BLOCKDAGLU_DATASET_SHA256 }
if ([string]::IsNullOrWhiteSpace($Token)) { $Token = $env:BLOCKDAGLU_DATASET_TOKEN }

$downloadDir = Join-Path $repoRoot "data\downloads"
$tempDir = Join-Path $downloadDir "extract"
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
  if ([string]::IsNullOrWhiteSpace($Url)) {
    throw "Provide -ArchivePath or set BLOCKDAGLU_DATASET_URL."
  }
  $ArchivePath = Join-Path $downloadDir "FinalTestCases.zip"
  $headers = @{}
  if (![string]::IsNullOrWhiteSpace($Token)) {
    $headers["Authorization"] = "Bearer $Token"
  }
  Write-Host "Downloading Final15 archive..."
  Invoke-WebRequest -Uri $Url -Headers $headers -OutFile $ArchivePath
}

$ArchivePath = (Resolve-Path $ArchivePath).Path
if ([string]::IsNullOrWhiteSpace($Sha256)) {
  throw "Provide -Sha256 or set BLOCKDAGLU_DATASET_SHA256."
}
$actualHash = (Get-FileHash -Algorithm SHA256 -Path $ArchivePath).Hash.ToLowerInvariant()
$expectedHash = $Sha256.Trim().ToLowerInvariant()
if ($actualHash -ne $expectedHash) {
  throw "Final15 SHA-256 mismatch. Expected $expectedHash, got $actualHash."
}

if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
Expand-Archive -Path $ArchivePath -DestinationPath $tempDir -Force

$candidate = $tempDir
if (Test-Path (Join-Path $tempDir "FinalTestCases")) {
  $candidate = Join-Path $tempDir "FinalTestCases"
} else {
  $nested = Get-ChildItem -Path $tempDir -Directory -Recurse |
    Where-Object { $_.Name -eq "FinalTestCases" } |
    Select-Object -First 1
  if ($null -ne $nested) { $candidate = $nested.FullName }
}

for ($caseId = 1; $caseId -le 15; $caseId++) {
  $caseDir = Join-Path $candidate $caseId
  if (!(Test-Path $caseDir -PathType Container)) {
    throw "Final15 archive is missing case directory: $caseId"
  }
  foreach ($suffix in @("JacobiMatrixInfo.txt", "VarInfo.csv", "ResidualValue.csv", "PerturbVarValue.csv")) {
    $expectedFile = Join-Path $caseDir ("{0}_{1}" -f $caseId, $suffix)
    if (!(Test-Path $expectedFile -PathType Leaf)) {
      throw "Final15 archive is missing: $expectedFile"
    }
  }
}

if (Test-Path $Destination) {
  if (!$Force) {
    throw "Destination already exists: $Destination. Re-run with -Force to replace it."
  }
  Remove-Item -Recurse -Force $Destination
}
New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
Copy-Item -Recurse -Path $candidate -Destination $Destination

Write-Host "Final15 installed and verified."
Write-Host "  destination: $Destination"
Write-Host "  sha256:      $actualHash"
