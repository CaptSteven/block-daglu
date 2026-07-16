param(
  [string]$Config = "Release",
  [string]$CasesRoot = "",
  [ValidateRange(0, 15)]
  [int]$CaseId = 0,
  [ValidateRange(1, 20)]
  [int]$Warmup = 1,
  [ValidateRange(1, 100)]
  [int]$Repetitions = 10
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($CasesRoot)) {
  $CasesRoot = (Join-Path $repoRoot "data\\FinalTestCases")
}

$toolBin = Join-Path $repoRoot "third_party\\SparseMatrixLUTestTool\\tool\\lib\\x64\\$Config"
$ssBin = Join-Path $repoRoot "third_party\\SuiteSparse\\bin\\x64\\$Config"

if (!(Test-Path $toolBin)) {
  # Most users only need Release DLLs; fall back if Debug bundle isn't present.
  $toolBin = Join-Path $repoRoot "third_party\\SparseMatrixLUTestTool\\tool\\lib\\x64\\Release"
}
if (!(Test-Path $ssBin)) {
  $ssBin = Join-Path $repoRoot "third_party\\SuiteSparse\\bin\\x64\\Release"
}
if (!(Test-Path $ssBin) -and ![string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
  $vcpkgTriplet = if ([string]::IsNullOrWhiteSpace($env:VCPKG_TARGET_TRIPLET)) {
    "x64-windows"
  } else {
    $env:VCPKG_TARGET_TRIPLET
  }
  $ssBin = Join-Path $env:VCPKG_ROOT "installed\\$vcpkgTriplet\\bin"
}

if (!(Test-Path $toolBin)) { throw "Missing tool bin dir: $toolBin" }
if (!(Test-Path $ssBin)) {
  throw "Missing SuiteSparse runtime directory. Install suitesparse with vcpkg or provide third_party\\SuiteSparse\\bin."
}
if (!(Test-Path $CasesRoot)) { throw "Missing cases root: $CasesRoot" }

$env:PATH = "$toolBin;$ssBin;$env:PATH"

$exe = Join-Path $repoRoot ("build\\blockdaglu\\{0}\\blockdaglu_benchmark.exe" -f $Config)
if (!(Test-Path $exe) -and $Config -ne "Release") {
  # If you built a single-config generator (e.g., Ninja), the exe may sit directly under build dir.
  $exe = Join-Path $repoRoot "build\\blockdaglu\\blockdaglu_benchmark.exe"
}

if (!(Test-Path $exe)) {
  throw "Benchmark exe not found: $exe`nBuild first (see docs/BUILD.md)."
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repoRoot ("bench\\out\\{0}" -f $stamp)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$env:BLOCKDAGLU_BENCH_OUT = $outDir
$env:BLOCKDAGLU_BENCH_WARMUP = $Warmup
$env:BLOCKDAGLU_BENCH_REPETITIONS = $Repetitions

Write-Host "Running benchmark..."
Write-Host "  exe:  $exe"
Write-Host "  data: $CasesRoot"
Write-Host "  PATH prepended: $toolBin ; $ssBin"
Write-Host "  out:  $outDir"
Write-Host "  runs: $Warmup warmup + $Repetitions measured"
if ($CaseId -gt 0) {
  Write-Host "  case: $CaseId"
}

if ($CaseId -gt 0) {
  & $exe $CasesRoot $CaseId
} else {
  & $exe $CasesRoot
}
if ($LASTEXITCODE -ne 0) {
  throw "Benchmark failed correctness or precision gates (exit code $LASTEXITCODE)."
}

$gitCommit = (& git -C $repoRoot rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0) { $gitCommit = "unknown" }
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$metadata = [ordered]@{
  timestamp_utc = [DateTime]::UtcNow.ToString("o")
  git_commit = "$gitCommit".Trim()
  os = [System.Environment]::OSVersion.VersionString
  cpu = $cpu.Name
  logical_processors = $cpu.NumberOfLogicalProcessors
  config = $Config
  warmup = $Warmup
  repetitions = $Repetitions
  cases_root = (Resolve-Path $CasesRoot).Path
  dataset_sha256 = $env:BLOCKDAGLU_DATASET_SHA256
  blockdaglu_mode = $env:BLOCKDAGLU_MODE
  blockdaglu_threads = $env:BLOCKDAGLU_THREADS
}
$metadata | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $outDir "metadata.json")

Write-Host ""
Write-Host "Benchmark outputs:"
Write-Host "  $outDir"
if (Test-Path (Join-Path $outDir "summary.txt")) {
  Write-Host ""
  Write-Host "summary.txt:"
  Get-Content (Join-Path $outDir "summary.txt")
}
