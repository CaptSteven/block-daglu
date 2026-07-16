param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  # ABAB rounds. Each round runs off once then on once.
  [Parameter(Mandatory = $false)]
  [ValidateRange(1, 9)]
  [int]$Rounds = 3,

  # Forwarded into compare_lutest_final_single.ps1. Use 0 to let algorithm self-select.
  [Parameter(Mandatory = $false)]
  [int]$Threads = 0,

  [Parameter(Mandatory = $false)]
  [ValidateSet("default", "auto", "klu", "block")]
  [string]$Mode = "auto",

  [Parameter(Mandatory = $false)]
  [string]$FinalRoot = "",

  [Parameter(Mandatory = $true)]
  [string]$CandidateDll,

  [Parameter(Mandatory = $false)]
  [string]$Tag = "abab_cond_refine"
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

function Stop-LeftoverProcesses {
  Get-Process SparseMatrixLUTest -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

function Run-Single([string]$which, [int]$round, [bool]$condOn) {
  $single = Join-Path $PSScriptRoot "compare_lutest_final_single.ps1"
  $params = @{
    Config = $Config
    Reps = 1
    Threads = $Threads
    Mode = $Mode
    CandidateDll = $CandidateDll
    Tag = ("{0}_r{1}_{2}" -f $Tag, $round, $which)
  }
  if ($FinalRoot) { $params.FinalRoot = $FinalRoot }

  if ($condOn) {
    $env:USERALGO_COND_REFINE = "1"
  } else {
    Remove-Item Env:USERALGO_COND_REFINE -ErrorAction SilentlyContinue
  }

  Stop-LeftoverProcesses
  $out = & $single @params
  if ($LASTEXITCODE -ne 0) { throw "compare_lutest_final_single failed: which=$which round=$round" }
  return $out.out_dir
}

function Read-Summary([string]$dir) {
  $s = (Import-Csv (Join-Path $dir "summary.csv"))[0]
  return [pscustomobject]@{
    weighted_baseline_ms = [double]$s.weighted_baseline_median_ms
    weighted_candidate_ms = [double]$s.weighted_candidate_median_ms
    speedup = [double]$s.speedup_baseline_over_candidate
  }
}

function Read-CaseTimes([string]$dir) {
  return Import-Csv (Join-Path $dir "case_times_median.csv")
}

$repo = Resolve-RepoRoot
if (-not $FinalRoot) { $FinalRoot = Join-Path $repo "SparseMatrixLUTest_Final" }

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repo ("build\\lutest_final_abab_cond_refine_{0}_{1}" -f $ts, $Tag)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("OutDir:   {0}" -f $outDir)
Write-Host ("Config:   {0}  Mode: {1}  Threads: {2}  Rounds: {3}" -f $Config, $Mode, $Threads, $Rounds)
Write-Host ("DLL:      {0}" -f $CandidateDll)

$runs = @()
$caseAll = @{}

for ($r = 1; $r -le $Rounds; $r++) {
  Write-Host ("Round {0}/{1} - refine=off..." -f $r, $Rounds)
  $dirOff = Run-Single "off" $r $false
  $runs += [pscustomobject]@{ round = $r; which = "off"; dir = $dirOff }
  $caseAll[$dirOff] = Read-CaseTimes $dirOff

  Write-Host ("Round {0}/{1} - refine=on..." -f $r, $Rounds)
  $dirOn = Run-Single "on" $r $true
  $runs += [pscustomobject]@{ round = $r; which = "on"; dir = $dirOn }
  $caseAll[$dirOn] = Read-CaseTimes $dirOn
}

$raw = @()
foreach ($r in $runs) {
  $s = Read-Summary $r.dir
  $raw += [pscustomobject]@{
    round = $r.round
    which = $r.which
    out_dir = $r.dir
    weighted_baseline_ms = $s.weighted_baseline_ms
    weighted_candidate_ms = $s.weighted_candidate_ms
    speedup_baseline_over_candidate = $s.speedup
  }
}
$rawCsv = Join-Path $outDir "abab_runs_raw.csv"
$raw | Export-Csv -NoTypeInformation -Encoding UTF8 $rawCsv

$agg = @()
foreach ($w in @("off", "on")) {
  $subset = $raw | Where-Object { $_.which -eq $w }
  $agg += [pscustomobject]@{
    which = $w
    runs = $subset.Count
    weighted_baseline_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_baseline_ms }))
    weighted_candidate_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_candidate_ms }))
  }
}
$aggCsv = Join-Path $outDir "abab_weighted_median_by_mode.csv"
$agg | Export-Csv -NoTypeInformation -Encoding UTF8 $aggCsv

$caseIds = 1..15 | ForEach-Object { "TestCase_$($_)" }
$caseRows = @()
foreach ($tc in $caseIds) {
  $rowBase = $null
  $vals = @{}
  foreach ($w in @("off", "on")) {
    $xs = @()
    foreach ($r in $runs | Where-Object { $_.which -eq $w }) {
      $t = $caseAll[$r.dir] | Where-Object { $_.TestCase -eq $tc } | Select-Object -First 1
      if ($null -ne $t) { $xs += [double]$t.CandidateTotalMs }
      if (-not $rowBase -and $null -ne $t) { $rowBase = $t }
    }
    $vals[$w] = Median ([double[]]$xs)
  }
  $off = [double]$vals["off"]
  $on = [double]$vals["on"]
  $caseRows += [pscustomobject]@{
    TestCase = $tc
    N = if ($rowBase) { [int]$rowBase.N } else { 0 }
    Weight = if ($rowBase) { [double]$rowBase.Weight } else { [double]::NaN }
    CandidateTotalMs_Off = $off
    CandidateTotalMs_On = $on
    Speedup_Off_over_On = if ($on -eq 0) { [double]::NaN } else { $off / $on }
    DeltaMs_On_minus_Off = $on - $off
  }
}
$caseCsv = Join-Path $outDir "abab_case_candidate_median.csv"
$caseRows | Export-Csv -NoTypeInformation -Encoding UTF8 $caseCsv

$md = Join-Path $outDir "README.md"
$lines = @()
$lines += "# ABAB Conditioned Refinement Report"
$lines += ""
$lines += "- Mode: $Mode"
$lines += "- Threads: $Threads"
$lines += "- Rounds: $Rounds (off then on per round)"
$lines += "- CandidateDll: $CandidateDll"
$lines += ""
$lines += "## Weighted (median)"
$lines += ""
$lines += "| which | runs | weighted_baseline_median_ms | weighted_candidate_median_ms |"
$lines += "|---:|---:|---:|---:|"
foreach ($r in $agg) {
  $lines += ("| {0} | {1} | {2:N3} | {3:N3} |" -f $r.which, $r.runs, $r.weighted_baseline_median_ms, $r.weighted_candidate_median_ms)
}
$lines += ""
$lines += "## Per-case candidate totals (median)"
$lines += ""
$lines += "| TestCase | N | Weight | off_ms | on_ms | off/on |"
$lines += "|---:|---:|---:|---:|---:|---:|"
foreach ($r in $caseRows) {
  $lines += ("| {0} | {1} | {2:N2} | {3:N3} | {4:N3} | {5:N3} |" -f $r.TestCase, $r.N, $r.Weight, $r.CandidateTotalMs_Off, $r.CandidateTotalMs_On, $r.Speedup_Off_over_On)
}
$lines | Set-Content -Encoding UTF8 $md

Write-Host ("Raw runs:   {0}" -f $rawCsv)
Write-Host ("Weighted:   {0}" -f $aggCsv)
Write-Host ("Per-case:   {0}" -f $caseCsv)
Write-Host ("Report MD:  {0}" -f $md)

[pscustomobject]@{
  out_dir = $outDir
  raw_csv = $rawCsv
  weighted_csv = $aggCsv
  per_case_csv = $caseCsv
  report_md = $md
}

