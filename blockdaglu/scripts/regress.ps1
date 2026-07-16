param(
  [Parameter(Mandatory = $false)]
  [string]$CasesPath = "SparseMatrixLUTest\\PreliminaryTestCases",

  [Parameter(Mandatory = $false)]
  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [int]$Threads = 0
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\\..")
$cases = Resolve-Path (Join-Path $root $CasesPath)

$exe = Join-Path $root ("build\\user_algorithm\\{0}\\useralgo_benchmark.exe" -f $Config)
if (-not (Test-Path $exe)) {
  throw "Missing benchmark exe: $exe (build first: cmake --build build/user_algorithm --config $Config)"
}

# Add organizer DLL directory to PATH for klu/btf/amd/colamd dependencies.
$dllDir = Join-Path $root "SparseMatrixLUTest\\x64\\Release"
if (-not (Test-Path $dllDir)) {
  throw "Missing DLL dir: $dllDir"
}
$env:PATH = "$dllDir;$env:PATH"

# Default: auto mode (per-matrix), refinement off.
Remove-Item Env:USERALGO_MODE -ErrorAction SilentlyContinue
$env:USERALGO_REFINE_ITERS = "0"
if ($Threads -gt 0) { $env:USERALGO_THREADS = "$Threads" }

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$out = Join-Path $root ("build\\regress_{0}.txt" -f $ts)

& $exe $cases | Tee-Object -FilePath $out | Out-Null

# Parse the "case,..." section and ensure pass_user_le_eps == 1 for all cases.
$lines = Get-Content $out
$caseLines = $lines | Where-Object { $_ -match '^[1-9],\d+,' }
if (-not $caseLines -or $caseLines.Count -lt 7) {
  throw "Failed to parse per-case pass lines from $out"
}

$failed = @()
foreach ($ln in $caseLines) {
  $cols = $ln.Split(',')
  $caseId = $cols[0]
  $pass = $cols[-1]
  if ($pass -ne "1") { $failed += $caseId }
}

if ($failed.Count -gt 0) {
  throw ("Accuracy gate failed for cases: {0}. See log: {1}" -f ($failed -join ","), $out)
}

Write-Host ("OK: all cases pass (err_user <= eps_suitesparse). Log: {0}" -f $out)
