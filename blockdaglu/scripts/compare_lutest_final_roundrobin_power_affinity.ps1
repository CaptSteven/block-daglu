param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  # Number of round-robin rounds. Each round runs 4 configs once.
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
  [string]$Tag = "power_affinity_2x2"
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

function Set-EnvForConfig([int]$power, [string]$affinity) {
  $env:USERALGO_POWER = "$power"
  if ($affinity -eq "off") {
    Remove-Item Env:USERALGO_AFFINITY -ErrorAction SilentlyContinue
  } else {
    $env:USERALGO_AFFINITY = $affinity
  }
}

function Run-Single([int]$power, [string]$affinity, [int]$round) {
  Set-EnvForConfig $power $affinity

  $single = Join-Path $PSScriptRoot "compare_lutest_final_single.ps1"
  $params = @{
    Config = $Config
    Reps = 1
    Threads = $Threads
    Mode = $Mode
    CandidateDll = $CandidateDll
    Tag = ("{0}_r{1}_p{2}_{3}" -f $Tag, $round, $power, $affinity)
  }
  if ($FinalRoot) { $params.FinalRoot = $FinalRoot }

  Stop-LeftoverProcesses
  $out = & $single @params
  if ($LASTEXITCODE -ne 0) { throw "compare_lutest_final_single failed: power=$power affinity=$affinity round=$round" }
  if (-not $out.out_dir) { throw "Missing out_dir from compare output: power=$power affinity=$affinity round=$round" }
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
$outDir = Join-Path $repo ("build\\lutest_final_power_affinity_rr_{0}_{1}" -f $ts, $Tag)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("OutDir:   {0}" -f $outDir)
Write-Host ("Config:   {0}  Mode: {1}  Threads: {2}  Rounds: {3}" -f $Config, $Mode, $Threads, $Rounds)
Write-Host ("Configs:  (power,affinity) in { (0,off), (0,pcores), (1,off), (1,pcores) }")

$configs = @(
  @{ power = 0; affinity = "off" },
  @{ power = 0; affinity = "pcores" },
  @{ power = 1; affinity = "off" },
  @{ power = 1; affinity = "pcores" }
)

$runs = @()
for ($r = 1; $r -le $Rounds; $r++) {
  foreach ($c in $configs) {
    Write-Host ("Round {0}/{1} - power={2} affinity={3}..." -f $r, $Rounds, $c.power, $c.affinity)
    $dir = Run-Single $c.power $c.affinity $r
    $runs += [pscustomobject]@{
      round = $r
      power = $c.power
      affinity = $c.affinity
      out_dir = $dir
    }
  }
}

# Raw per-run numbers
$rawRows = @()
$caseAll = @{}
foreach ($r in $runs) {
  $s = Read-Summary $r.out_dir
  $rawRows += [pscustomobject]@{
    round = $r.round
    power = $r.power
    affinity = $r.affinity
    out_dir = $r.out_dir
    weighted_baseline_ms = $s.weighted_baseline_ms
    weighted_candidate_ms = $s.weighted_candidate_ms
    speedup_baseline_over_candidate = $s.speedup
  }
  $caseAll[$r.out_dir] = Read-CaseTimes $r.out_dir
}

$rawCsv = Join-Path $outDir "rr_runs_raw.csv"
$rawRows | Export-Csv -NoTypeInformation -Encoding UTF8 $rawCsv

# Aggregate weighted medians per config
$aggRows = @()
foreach ($c in $configs) {
  $subset = $rawRows | Where-Object { $_.power -eq $c.power -and $_.affinity -eq $c.affinity }
  $aggRows += [pscustomobject]@{
    power = $c.power
    affinity = $c.affinity
    runs = $subset.Count
    weighted_baseline_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_baseline_ms }))
    weighted_candidate_median_ms = Median ([double[]]($subset | ForEach-Object { $_.weighted_candidate_ms }))
  }
}
$aggCsv = Join-Path $outDir "rr_weighted_median_by_config.csv"
$aggRows | Export-Csv -NoTypeInformation -Encoding UTF8 $aggCsv

# Per-case candidate totals (median across rounds) per config
$caseIds = 1..15 | ForEach-Object { "TestCase_$($_)" }
$caseRows = @()
foreach ($tc in $caseIds) {
  # Use first observed row as the source of N/Weight
  $rowBase = $null
  $vals = @{}

  foreach ($c in $configs) {
    $xs = @()
    foreach ($r in $runs | Where-Object { $_.power -eq $c.power -and $_.affinity -eq $c.affinity }) {
      $t = $caseAll[$r.out_dir] | Where-Object { $_.TestCase -eq $tc } | Select-Object -First 1
      if ($null -ne $t) { $xs += [double]$t.CandidateTotalMs }
      if (-not $rowBase -and $null -ne $t) { $rowBase = $t }
    }
    $key = ("p{0}_{1}" -f $c.power, $c.affinity)
    $vals[$key] = Median ([double[]]$xs)
  }

  $caseRows += [pscustomobject]@{
    TestCase = $tc
    N = if ($rowBase) { [int]$rowBase.N } else { 0 }
    Weight = if ($rowBase) { [double]$rowBase.Weight } else { [double]::NaN }
    CandidateTotalMs_p0_off = [double]$vals["p0_off"]
    CandidateTotalMs_p0_pcores = [double]$vals["p0_pcores"]
    CandidateTotalMs_p1_off = [double]$vals["p1_off"]
    CandidateTotalMs_p1_pcores = [double]$vals["p1_pcores"]
  }
}

$caseCsv = Join-Path $outDir "rr_case_candidate_median.csv"
$caseRows | Export-Csv -NoTypeInformation -Encoding UTF8 $caseCsv

$md = Join-Path $outDir "README.md"
$lines = @()
$lines += "# Power × Affinity 2×2 Round-Robin Report"
$lines += ""
$lines += "- Config: $Config"
$lines += "- Mode: $Mode"
$lines += "- Threads: $Threads"
$lines += "- Rounds: $Rounds (each round runs 4 configs once)"
$lines += "- Configs: (power,affinity) in { (0,off), (0,pcores), (1,off), (1,pcores) }"
$lines += ""
$lines += "## Weighted (median)"
$lines += ""
$lines += "| power | affinity | runs | weighted_baseline_median_ms | weighted_candidate_median_ms |"
$lines += "|---:|---:|---:|---:|---:|"
foreach ($r in $aggRows) {
  $lines += ("| {0} | {1} | {2} | {3:N3} | {4:N3} |" -f $r.power, $r.affinity, $r.runs, $r.weighted_baseline_median_ms, $r.weighted_candidate_median_ms)
}
$lines += ""
$lines += "## Per-case candidate totals (median)"
$lines += ""
$lines += "| TestCase | N | Weight | p0_off | p0_pcores | p1_off | p1_pcores |"
$lines += "|---:|---:|---:|---:|---:|---:|---:|"
foreach ($r in $caseRows) {
  $lines += ("| {0} | {1} | {2:N2} | {3:N3} | {4:N3} | {5:N3} | {6:N3} |" -f $r.TestCase, $r.N, $r.Weight, $r.CandidateTotalMs_p0_off, $r.CandidateTotalMs_p0_pcores, $r.CandidateTotalMs_p1_off, $r.CandidateTotalMs_p1_pcores)
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

