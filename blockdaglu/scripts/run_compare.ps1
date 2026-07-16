param(
  [Parameter(Mandatory = $false)]
  [string]$CasesPath = "SparseMatrixLUTest\\PreliminaryTestCases",

  [Parameter(Mandatory = $false)]
  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [int]$Threads = 0,

  [Parameter(Mandatory = $false)]
  [int]$Runs = 3
)

$ErrorActionPreference = "Continue"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\\..")
$cases = Resolve-Path (Join-Path $root $CasesPath)

$exe = Join-Path $root ("build\\user_algorithm\\{0}\\useralgo_benchmark.exe" -f $Config)
if (-not (Test-Path $exe)) { throw "Missing exe: $exe" }

$dllDir = Join-Path $root "SparseMatrixLUTest\\x64\\Release"
if (-not (Test-Path $dllDir)) { throw "Missing DLL dir: $dllDir" }
$env:PATH = "$dllDir;$env:PATH"

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $root ("build\\compare_{0}" -f $ts)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if ($Threads -gt 0) { $env:USERALGO_THREADS = "$Threads" } else { Remove-Item Env:USERALGO_THREADS -ErrorAction SilentlyContinue }
$env:USERALGO_REFINE_ITERS = "0"
$env:USERALGO_PROFILE = "0"
$env:USERALGO_SUPERBLOCK_STATS = "0"
$env:USERALGO_SUPERBLOCK_BBD = "0"

Write-Host ("Output dir: {0}" -f $outDir)

for ($i=1; $i -le $Runs; $i++) {
  Write-Host ("Run {0}/{1}: KLU" -f $i, $Runs)
  $env:USERALGO_MODE = "klu"
  $logK = Join-Path $outDir ("bench_klu_{0}.txt" -f $i)
  & $exe $cases 2>&1 | Out-String | Set-Content -Encoding Unicode $logK

  Write-Host ("Run {0}/{1}: AUTO" -f $i, $Runs)
  Remove-Item Env:USERALGO_MODE -ErrorAction SilentlyContinue
  $logA = Join-Path $outDir ("bench_auto_{0}.txt" -f $i)
  & $exe $cases 2>&1 | Out-String | Set-Content -Encoding Unicode $logA
}

Write-Host ("Done. Logs: {0}" -f $outDir)
