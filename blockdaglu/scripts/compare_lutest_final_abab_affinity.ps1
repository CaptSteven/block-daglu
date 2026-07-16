param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  # Number of ABAB rounds. Each round runs A once then B once.
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

  # A/B affinity configs: off|pcores|ecores
  [Parameter(Mandatory = $false)]
  [ValidateSet("off", "pcores", "ecores")]
  [string]$AffinityA = "off",

  [Parameter(Mandatory = $false)]
  [ValidateSet("off", "pcores", "ecores")]
  [string]$AffinityB = "pcores",

  [Parameter(Mandatory = $false)]
  [string]$Tag = "affinity_abab"
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

function Run-Single([string]$affinity, [string]$tagSuffix) {
  if ($affinity -eq "off") {
    Remove-Item Env:USERALGO_AFFINITY -ErrorAction SilentlyContinue
  } else {
    $env:USERALGO_AFFINITY = $affinity
  }

  $single = Join-Path $PSScriptRoot "compare_lutest_final_single.ps1"
  $params = @{
    Config = $Config
    Reps = 1
    Threads = $Threads
    Mode = $Mode
    CandidateDll = $CandidateDll
    Tag = ("{0}_{1}_{2}" -f $Tag, $tagSuffix, $affinity)
  }
  if ($FinalRoot) { $params.FinalRoot = $FinalRoot }

  Stop-LeftoverProcesses
  $out = & $single @params
  if ($LASTEXITCODE -ne 0) { throw "compare_lutest_final_single failed: affinity=$affinity tag=$tagSuffix" }

  $outDir = $out.out_dir
  if (-not $outDir) { throw "Failed to parse out_dir from compare output (affinity=$affinity)." }
  return $outDir
}

$repo = Resolve-RepoRoot
if (-not $FinalRoot) { $FinalRoot = Join-Path $repo "SparseMatrixLUTest_Final" }

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repo ("build\\lutest_final_affinity_abab_{0}_{1}" -f $ts, $Tag)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("OutDir:   {0}" -f $outDir)
Write-Host ("Config:   {0}  Mode: {1}  Threads: {2}  Rounds: {3}" -f $Config, $Mode, $Threads, $Rounds)
Write-Host ("Affinity: A={0}  B={1}" -f $AffinityA, $AffinityB)

$runs = @()

for ($r = 1; $r -le $Rounds; $r++) {
  $tagA = ("r{0}_A" -f $r)
  $tagB = ("r{0}_B" -f $r)

  Write-Host ("Round {0}/{1} - A({2})..." -f $r, $Rounds, $AffinityA)
  $dirA = Run-Single $AffinityA $tagA
  $runs += [pscustomobject]@{ round = $r; which = "A"; affinity = $AffinityA; dir = $dirA }

  Write-Host ("Round {0}/{1} - B({2})..." -f $r, $Rounds, $AffinityB)
  $dirB = Run-Single $AffinityB $tagB
  $runs += [pscustomobject]@{ round = $r; which = "B"; affinity = $AffinityB; dir = $dirB }
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

# Collect raw per-run numbers
$rawRows = @()
foreach ($r in $runs) {
  $s = Read-Summary $r.dir
  $rawRows += [pscustomobject]@{
    round = $r.round
    which = $r.which
    affinity = $r.affinity
    out_dir = $r.dir
    weighted_baseline_ms = $s.weighted_baseline_ms
    weighted_candidate_ms = $s.weighted_candidate_ms
    speedup_baseline_over_candidate = $s.speedup
  }
}

$rawCsv = Join-Path $outDir "abab_runs_raw.csv"
$rawRows | Export-Csv -NoTypeInformation -Encoding UTF8 $rawCsv

# Aggregate: per-affinity median (weighted + per-case candidate totals)
$caseAll = @{}
foreach ($r in $runs) {
  $caseAll[$r.dir] = Read-CaseTimes $r.dir
}

function CaseKey([string]$tc) { return $tc }

$affs = @($AffinityA, $AffinityB) | Select-Object -Unique
$caseIds = 1..15 | ForEach-Object { "TestCase_$($_)" }

$aggRows = @()
foreach ($a in $affs) {
  $subset = $rawRows | Where-Object { $_.affinity -eq $a }
  $aggRows += [pscustomobject]@{
    affinity = $a
    runs = $subset.Count
    weighted_baseline_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_baseline_ms }))
    weighted_candidate_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_candidate_ms }))
  }
}

$aggCsv = Join-Path $outDir "abab_weighted_median_by_affinity.csv"
$aggRows | Export-Csv -NoTypeInformation -Encoding UTF8 $aggCsv

# Per-case candidate totals (median across rounds for each affinity)
$caseRows = @()
foreach ($tc in $caseIds) {
  $rowBase = $null
  $vals = @{}
  foreach ($a in $affs) {
    $xs = @()
    foreach ($r in $runs | Where-Object { $_.affinity -eq $a }) {
      $t = $caseAll[$r.dir] | Where-Object { $_.TestCase -eq $tc } | Select-Object -First 1
      if ($null -ne $t) { $xs += [double]$t.CandidateTotalMs }
      if (-not $rowBase -and $null -ne $t) { $rowBase = $t }
    }
    $vals[$a] = Median ([double[]]$xs)
  }

  $a0 = $AffinityA
  $b0 = $AffinityB
  $off = [double]$vals[$a0]
  $pc = [double]$vals[$b0]
  $ratio = if ($pc -eq 0) { [double]::NaN } else { $off / $pc }

  $caseRows += [pscustomobject]@{
    TestCase = $tc
    N = if ($rowBase) { [int]$rowBase.N } else { 0 }
    Weight = if ($rowBase) { [double]$rowBase.Weight } else { [double]::NaN }
    CandidateTotalMs_A = $off
    CandidateTotalMs_B = $pc
    Speedup_A_over_B = $ratio
  }
}

$caseCsv = Join-Path $outDir "abab_case_candidate_median.csv"
$caseRows | Export-Csv -NoTypeInformation -Encoding UTF8 $caseCsv

$md = Join-Path $outDir "README.md"
$lines = @()
$lines += "# Affinity ABAB Report"
$lines += ""
$lines += "- Config: $Config"
$lines += "- Mode: $Mode"
$lines += "- Threads: $Threads"
$lines += "- Rounds: $Rounds (ABAB; each affinity run once per round)"
$lines += "- A: USERALGO_AFFINITY=$AffinityA"
$lines += "- B: USERALGO_AFFINITY=$AffinityB"
$lines += ""
$lines += "## Weighted (median)"
$lines += ""
$lines += "| affinity | runs | weighted_baseline_median_ms | weighted_candidate_median_ms |"
$lines += "|---:|---:|---:|---:|"
foreach ($r in $aggRows) {
  $lines += ("| {0} | {1} | {2:N3} | {3:N3} |" -f $r.affinity, $r.runs, $r.weighted_baseline_median_ms, $r.weighted_candidate_median_ms)
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

Write-Host ("Raw runs:    {0}" -f $rawCsv)
Write-Host ("Weighted:    {0}" -f $aggCsv)
Write-Host ("Per-case:    {0}" -f $caseCsv)
Write-Host ("Report MD:   {0}" -f $md)

[pscustomobject]@{
  out_dir = $outDir
  raw_csv = $rawCsv
  weighted_csv = $aggCsv
  per_case_csv = $caseCsv
  report_md = $md
}
