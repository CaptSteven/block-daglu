param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  # ABAB rounds. Each round runs A once then B once.
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
  [string]$CandidateDllA,

  [Parameter(Mandatory = $true)]
  [string]$CandidateDllB,

  # Optional extra runtime DLLs to be copied next to SparseMatrixLUTest.exe before running each candidate.
  # Use this for candidates that depend on additional DLLs (e.g., OpenBLAS/OpenMP) not shipped in the Final framework.
  [Parameter(Mandatory = $false)]
  [string[]]$ExtraRuntimeDllsA = @(),

  [Parameter(Mandatory = $false)]
  [string[]]$ExtraRuntimeDllsB = @(),

  [Parameter(Mandatory = $false)]
  [string]$Tag = "abab_two_candidates"
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

function Run-Single([string]$dll, [string[]]$extraDlls, [string]$which, [int]$round) {
  $single = Join-Path $PSScriptRoot "compare_lutest_final_single.ps1"
  $params = @{
    Config = $Config
    Reps = 1
    Threads = $Threads
    Mode = $Mode
    CandidateDll = $dll
    ExtraRuntimeDlls = $extraDlls
    Tag = ("{0}_r{1}_{2}" -f $Tag, $round, $which)
  }
  if ($FinalRoot) { $params.FinalRoot = $FinalRoot }

  Stop-LeftoverProcesses
  $out = & $single @params
  if ($LASTEXITCODE -ne 0) { throw "compare_lutest_final_single failed: which=$which round=$round" }
  if (-not $out.out_dir) { throw "Missing out_dir from compare output: which=$which round=$round" }
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

# Ensure both A/B use compiled-in defaults (do not override with env here).
Remove-Item Env:USERALGO_PROFILE,Env:USERALGO_MODE,Env:USERALGO_THREADS,Env:USERALGO_REFINE_ITERS -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_AFFINITY,Env:USERALGO_POWER -ErrorAction SilentlyContinue

$repo = Resolve-RepoRoot
if (-not $FinalRoot) { $FinalRoot = Join-Path $repo "SparseMatrixLUTest_Final" }

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repo ("build\\lutest_final_abab_two_candidates_{0}_{1}" -f $ts, $Tag)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("OutDir:   {0}" -f $outDir)
Write-Host ("Config:   {0}  Mode: {1}  Threads: {2}  Rounds: {3}" -f $Config, $Mode, $Threads, $Rounds)
Write-Host ("A DLL:    {0}" -f $CandidateDllA)
Write-Host ("B DLL:    {0}" -f $CandidateDllB)

$runs = @()
for ($r = 1; $r -le $Rounds; $r++) {
  # Alternate order per round to reduce bias from "second run is hotter/slower".
  if (($r % 2) -eq 1) {
    Write-Host ("Round {0}/{1} - A..." -f $r, $Rounds)
    $dirA = Run-Single $CandidateDllA $ExtraRuntimeDllsA "A" $r
    $runs += [pscustomobject]@{ round = $r; which = "A"; dir = $dirA }

    Write-Host ("Round {0}/{1} - B..." -f $r, $Rounds)
    $dirB = Run-Single $CandidateDllB $ExtraRuntimeDllsB "B" $r
    $runs += [pscustomobject]@{ round = $r; which = "B"; dir = $dirB }
  } else {
    Write-Host ("Round {0}/{1} - B..." -f $r, $Rounds)
    $dirB = Run-Single $CandidateDllB $ExtraRuntimeDllsB "B" $r
    $runs += [pscustomobject]@{ round = $r; which = "B"; dir = $dirB }

    Write-Host ("Round {0}/{1} - A..." -f $r, $Rounds)
    $dirA = Run-Single $CandidateDllA $ExtraRuntimeDllsA "A" $r
    $runs += [pscustomobject]@{ round = $r; which = "A"; dir = $dirA }
  }
}

$rawRows = @()
$caseAll = @{}
foreach ($r in $runs) {
  $s = Read-Summary $r.dir
  $rawRows += [pscustomobject]@{
    round = $r.round
    which = $r.which
    out_dir = $r.dir
    weighted_baseline_ms = $s.weighted_baseline_ms
    weighted_candidate_ms = $s.weighted_candidate_ms
    speedup_baseline_over_candidate = $s.speedup
  }
  $caseAll[$r.dir] = Read-CaseTimes $r.dir
}

$rawCsv = Join-Path $outDir "abab_runs_raw.csv"
$rawRows | Export-Csv -NoTypeInformation -Encoding UTF8 $rawCsv

# Aggregate weighted medians per which
$aggRows = @()
foreach ($w in @("A", "B")) {
  $subset = $rawRows | Where-Object { $_.which -eq $w }
  $aggRows += [pscustomobject]@{
    which = $w
    runs = $subset.Count
    weighted_baseline_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_baseline_ms }))
    weighted_candidate_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_candidate_ms }))
  }
}
$aggCsv = Join-Path $outDir "abab_weighted_median_by_candidate.csv"
$aggRows | Export-Csv -NoTypeInformation -Encoding UTF8 $aggCsv

# Per-case candidate totals (median across rounds)
$caseIds = 1..15 | ForEach-Object { "TestCase_$($_)" }
$caseRows = @()
foreach ($tc in $caseIds) {
  $rowBase = $null
  $vals = @{}
  foreach ($w in @("A", "B")) {
    $xs = @()
    foreach ($r in $runs | Where-Object { $_.which -eq $w }) {
      $t = $caseAll[$r.dir] | Where-Object { $_.TestCase -eq $tc } | Select-Object -First 1
      if ($null -ne $t) { $xs += [double]$t.CandidateTotalMs }
      if (-not $rowBase -and $null -ne $t) { $rowBase = $t }
    }
    $vals[$w] = Median ([double[]]$xs)
  }
  $a = [double]$vals["A"]
  $b = [double]$vals["B"]
  $caseRows += [pscustomobject]@{
    TestCase = $tc
    N = if ($rowBase) { [int]$rowBase.N } else { 0 }
    Weight = if ($rowBase) { [double]$rowBase.Weight } else { [double]::NaN }
    CandidateTotalMs_A = $a
    CandidateTotalMs_B = $b
    Speedup_A_over_B = if ($b -eq 0) { [double]::NaN } else { $a / $b }
    DeltaMs_B_minus_A = $b - $a
  }
}
$caseCsv = Join-Path $outDir "abab_case_candidate_median.csv"
$caseRows | Export-Csv -NoTypeInformation -Encoding UTF8 $caseCsv

$md = Join-Path $outDir "README.md"
$lines = @()
$lines += "# ABAB Two-Candidate Report"
$lines += ""
$lines += "- Mode: $Mode"
$lines += "- Threads: $Threads"
$lines += "- Rounds: $Rounds (A then B per round)"
$lines += "- A: $CandidateDllA"
$lines += "- B: $CandidateDllB"
$lines += ""
$lines += "## Weighted (median)"
$lines += ""
$lines += "| which | runs | weighted_baseline_median_ms | weighted_candidate_median_ms |"
$lines += "|---:|---:|---:|---:|"
foreach ($r in $aggRows) {
  $lines += ("| {0} | {1} | {2:N3} | {3:N3} |" -f $r.which, $r.runs, $r.weighted_baseline_median_ms, $r.weighted_candidate_median_ms)
}
$lines += ""
$lines += "## Per-case candidate totals (median)"
$lines += ""
$lines += "| TestCase | N | Weight | A_ms | B_ms | A/B |"
$lines += "|---:|---:|---:|---:|---:|---:|"
foreach ($r in $caseRows) {
  $lines += ("| {0} | {1} | {2:N2} | {3:N3} | {4:N3} | {5:N3} |" -f $r.TestCase, $r.N, $r.Weight, $r.CandidateTotalMs_A, $r.CandidateTotalMs_B, $r.Speedup_A_over_B)
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
