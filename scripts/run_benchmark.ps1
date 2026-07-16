param(
  [string]$Config = "Release",
  [string]$CasesRoot = "",
  [int]$CaseId = 0
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

if (!(Test-Path $toolBin)) { throw "Missing tool bin dir: $toolBin" }
if (!(Test-Path $ssBin)) { throw "Missing SuiteSparse bin dir: $ssBin" }
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

Write-Host "Running benchmark..."
Write-Host "  exe:  $exe"
Write-Host "  data: $CasesRoot"
Write-Host "  PATH prepended: $toolBin ; $ssBin"
Write-Host "  out:  $outDir"
if ($CaseId -gt 0) {
  Write-Host "  case: $CaseId"
}

if ($CaseId -gt 0) {
  & $exe $CasesRoot $CaseId
} else {
  & $exe $CasesRoot
}

Write-Host ""
Write-Host "Benchmark outputs:"
Write-Host "  $outDir"
if (Test-Path (Join-Path $outDir "summary.txt")) {
  Write-Host ""
  Write-Host "summary.txt:"
  Get-Content (Join-Path $outDir "summary.txt")
}
