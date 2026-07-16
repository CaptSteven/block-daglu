param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [ValidateRange(1, 9)]
  [int]$Reps = 3,

  [Parameter(Mandatory = $false)]
  [int]$Threads = 4,

  [Parameter(Mandatory = $false)]
  [ValidateSet("default", "auto", "klu", "block")]
  [string]$Mode = "auto",

  [Parameter(Mandatory = $false)]
  [string]$FinalRoot = "",

  [Parameter(Mandatory = $true)]
  [string]$CandidateDll,

  [Parameter(Mandatory = $false)]
  [string[]]$ExtraRuntimeDlls = @(),

  [Parameter(Mandatory = $false)]
  [string]$Tag = "candidate"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot { (Resolve-Path (Join-Path $PSScriptRoot "..\\..")).Path }

function Median([double[]]$xs) {
  if ($null -eq $xs -or $xs.Count -eq 0) { return [double]::NaN }
  $ys = $xs | Sort-Object
  $n = $ys.Count
  if (($n % 2) -eq 1) { return [double]$ys[($n - 1) / 2] }
  return 0.5 * ([double]$ys[$n / 2 - 1] + [double]$ys[$n / 2])
}

function Case-Weight([int]$n) {
  if ($n -le 20000) { return 0.1 }
  if ($n -le 40000) { return 0.15 }
  if ($n -le 55000) { return 0.25 }
  return 0.5
}

function Parse-CaseSizes([string]$logPath) {
  $text = Get-Content -Path $logPath -Raw -Encoding Default
  $map = @{}

  # Prefer ASCII-only parsing from the final summary table.
  # Example: "TestCase_7  1095.0  3576.2  ...  61531*61531  ..."
  $tableRx = [regex]'(?m)^TestCase_(\d+)\s+.*?\s+(\d+)\*(\d+)\s+'
  foreach ($m in $tableRx.Matches($text)) {
    $id = [int]$m.Groups[1].Value
    $n = [int]$m.Groups[2].Value
    $map[$id] = $n
  }

  return $map
}

function Read-PerfCsv([string]$csvPath) {
  $rows = Import-Csv -Path $csvPath
  $out = @{}
  foreach ($r in $rows) {
    $id = [int](($r.TestCase) -replace "TestCase_", "")
    $out[$id] = [pscustomobject]@{
      id = $id
      prep_ms = [double]$r."PreparationTime(ms)"
      rearr_ms = [double]$r."RearrangementTime(ms)"
      decomp_ms = [double]$r."DecompositionTime(ms)"
      total_ms = [double]$r."TotalTime(ms)"
    }
  }
  return $out
}

function Weighted-Total([hashtable]$caseSizes, [hashtable]$perfByCase) {
  $s = 0.0
  foreach ($id in $perfByCase.Keys) {
    $n = $caseSizes[$id]
    $w = Case-Weight $n
    $s += $w * [double]$perfByCase[$id].total_ms
  }
  return $s
}

function Run-One([string]$exe, [string]$dllDir, [string]$workDir, [string]$outDir, [string]$tag, [int]$algChoice) {
  $t = Get-Date -Format "yyyyMMdd_HHmmss_fff"
  $stdin = Join-Path $outDir ("{0}_{1}.stdin.txt" -f $tag, $t)
  (($algChoice.ToString()) + "`r`n0`r`n") | Set-Content -NoNewline -Encoding ASCII $stdin

  $log = Join-Path $outDir ("{0}_{1}.log.txt" -f $tag, $t)
  $cmdFile = Join-Path $outDir ("{0}_{1}.run.cmd" -f $tag, $t)

  $start = Get-Date
  @(
    "@echo off"
    "setlocal"
    "set ""PATH=$dllDir;%PATH%"""
    "pushd ""$workDir"""
    """$exe"" < ""$stdin"" > ""$log"" 2>&1"
    "set ERR=%ERRORLEVEL%"
    "popd"
    "exit /b %ERR%"
  ) | Set-Content -Encoding Default $cmdFile

  $null = cmd.exe /d /c """$cmdFile"""

  $perfCsv = Get-ChildItem -Path $workDir -Filter "performance_report_*.csv" -File -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $start.AddSeconds(-1) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

  $csvOut = $null
  if ($perfCsv) {
    $csvOut = Join-Path $outDir ("{0}_{1}" -f $tag, $perfCsv.Name)
    Copy-Item -Force $perfCsv.FullName $csvOut
  }

  [pscustomobject]@{ log = $log; csv = $csvOut }
}

$repo = Resolve-RepoRoot
if (-not $FinalRoot) { $FinalRoot = Join-Path $repo "SparseMatrixLUTest_Final" }
$FinalRoot = (Resolve-Path $FinalRoot).Path

$exe = Join-Path $FinalRoot ("x64\\{0}\\SparseMatrixLUTest.exe" -f $Config)
$dllDir = Join-Path $FinalRoot ("x64\\{0}" -f $Config)
$workDir = Join-Path $FinalRoot "SparseMatrixLUTest"
if (-not (Test-Path $exe)) { throw "Missing exe: $exe" }
if (-not (Test-Path $dllDir)) { throw "Missing dll dir: $dllDir" }
if (-not (Test-Path $workDir)) { throw "Missing work dir: $workDir" }
if (-not (Test-Path $CandidateDll)) { throw "Missing CandidateDll: $CandidateDll" }

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repo ("build\\lutest_final_{0}_{1}_{2}" -f $ts, $Tag, $Mode)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("OutDir:  {0}" -f $outDir)
Write-Host ("Final:   {0}" -f $FinalRoot)
Write-Host ("Config:  {0}  Reps: {1}  Mode: {2}  Threads: {3}" -f $Config, $Reps, $Mode, $Threads)

# Place candidate DLL next to SparseMatrixLUTest.exe
$dstDll = Join-Path $dllDir "useralgo_baseline.dll"
Copy-Item -Force $CandidateDll $dstDll
foreach ($d in $ExtraRuntimeDlls) {
  if ($d -and (Test-Path $d)) {
    Copy-Item -Force $d (Join-Path $dllDir (Split-Path $d -Leaf))
  }
}

# Candidate env (baseline ignores these, but we keep ABAB symmetry)
Remove-Item Env:USERALGO_MODE -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_THREADS -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_PROFILE -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_REFINE_ITERS -ErrorAction SilentlyContinue
$env:USERALGO_PROFILE = "0"
$env:USERALGO_REFINE_ITERS = "0"
if ($Mode -ne "default") { $env:USERALGO_MODE = $Mode }
if ($Threads -gt 0) { $env:USERALGO_THREADS = "$Threads" }

$bRuns = @()
$cRuns = @()

for ($i = 1; $i -le $Reps; $i++) {
  Write-Host ("baseline rep {0}/{1}..." -f $i, $Reps)
  $bRuns += Run-One $exe $dllDir $workDir $outDir ("baseline_r{0}" -f $i) 0
  Write-Host ("candidate rep {0}/{1}..." -f $i, $Reps)
  $cRuns += Run-One $exe $dllDir $workDir $outDir ("candidate_r{0}" -f $i) 1
}

if (-not $bRuns[0].log) { throw "Missing baseline log" }
$caseSizes = Parse-CaseSizes $bRuns[0].log
if (-not $caseSizes -or $caseSizes.Count -lt 15) {
  throw "Failed to parse case sizes from log (parsed $($caseSizes.Count))."
}

$wb = @()
$wc = @()

$bPerf = @()
$cPerf = @()

foreach ($r in $bRuns) {
  if (-not $r.csv) { throw "Missing baseline CSV" }
  $p = Read-PerfCsv $r.csv
  $bPerf += $p
  $wb += (Weighted-Total $caseSizes $p)
}
foreach ($r in $cRuns) {
  if (-not $r.csv) { throw "Missing candidate CSV" }
  $p = Read-PerfCsv $r.csv
  $cPerf += $p
  $wc += (Weighted-Total $caseSizes $p)
}

$wbMed = Median ([double[]]$wb)
$wcMed = Median ([double[]]$wc)
$speedup = if ([double]::IsNaN($wbMed) -or [double]::IsNaN($wcMed) -or $wcMed -eq 0.0) { [double]::NaN } else { $wbMed / $wcMed }

$summary = [pscustomobject]@{
  reps = $Reps
  mode = $Mode
  threads = $Threads
  weighted_baseline_median_ms = $wbMed
  weighted_candidate_median_ms = $wcMed
  speedup_baseline_over_candidate = $speedup
}
$summaryPath = Join-Path $outDir "summary.csv"
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath

$caseRows = @()
for ($id = 1; $id -le 15; $id++) {
  $n = [int]$caseSizes[$id]
  $w = Case-Weight $n

  $bPrep = Median ([double[]]($bPerf | ForEach-Object { $_[$id].prep_ms }))
  $bRearr = Median ([double[]]($bPerf | ForEach-Object { $_[$id].rearr_ms }))
  $bDecomp = Median ([double[]]($bPerf | ForEach-Object { $_[$id].decomp_ms }))
  $bTotal = Median ([double[]]($bPerf | ForEach-Object { $_[$id].total_ms }))

  $cPrep = Median ([double[]]($cPerf | ForEach-Object { $_[$id].prep_ms }))
  $cRearr = Median ([double[]]($cPerf | ForEach-Object { $_[$id].rearr_ms }))
  $cDecomp = Median ([double[]]($cPerf | ForEach-Object { $_[$id].decomp_ms }))
  $cTotal = Median ([double[]]($cPerf | ForEach-Object { $_[$id].total_ms }))
  $caseSpeedup = if ($cTotal -eq 0.0) { [double]::NaN } else { $bTotal / $cTotal }

  $caseRows += [pscustomobject]@{
    TestCase = "TestCase_$id"
    N = $n
    Weight = $w
    BaselinePrepMs = $bPrep
    BaselineRearrMs = $bRearr
    BaselineDecompMs = $bDecomp
    BaselineTotalMs = $bTotal
    CandidatePrepMs = $cPrep
    CandidateRearrMs = $cRearr
    CandidateDecompMs = $cDecomp
    CandidateTotalMs = $cTotal
    SpeedupTotal = $caseSpeedup
  }
}

$caseCsv = Join-Path $outDir "case_times_median.csv"
$caseRows | Export-Csv -NoTypeInformation -Encoding UTF8 $caseCsv

$md = Join-Path $outDir "case_times_median.md"
function F([double]$x) {
  if ([double]::IsNaN($x)) { return "" }
  return ("{0:N3}" -f $x)
}

$lines = @()
$lines += "| TestCase | N | Weight | BaselineTotalMs | CandidateTotalMs | SpeedupTotal | BaselineRearrMs | BaselineDecompMs | CandidateRearrMs | CandidateDecompMs |"
$lines += "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
foreach ($r in $caseRows) {
  $lines += ("| {0} | {1} | {2} | {3} | {4} | {5:N3} | {6} | {7} | {8} | {9} |" -f
    $r.TestCase,
    $r.N,
    $r.Weight,
    (F $r.BaselineTotalMs),
    (F $r.CandidateTotalMs),
    $r.SpeedupTotal,
    (F $r.BaselineRearrMs),
    (F $r.BaselineDecompMs),
    (F $r.CandidateRearrMs),
    (F $r.CandidateDecompMs)
  )
}
$lines | Set-Content -Encoding UTF8 $md

Write-Host ("Summary: {0}" -f $summaryPath)
Write-Host ("Cases:   {0}" -f $caseCsv)
Write-Host ("MD:      {0}" -f $md)

[pscustomobject]@{
  out_dir = $outDir
  summary_csv = $summaryPath
  case_times_csv = $caseCsv
}
