param(
  [Parameter(Mandatory = $false)]
  [ValidateRange(1, 9)]
  [int]$Reps = 3,

  [Parameter(Mandatory = $false)]
  [string]$OursRoot = "",

  [Parameter(Mandatory = $false)]
  [string]$TheirsRoot = "",

  [Parameter(Mandatory = $false)]
  [string]$OutDir = ""
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

function Set-HighPerfProcess([System.Diagnostics.Process]$p) {
  try { $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::High } catch {}
  try {
    $n = [int]$env:NUMBER_OF_PROCESSORS
    if ($n -gt 0) {
      $bits = [Math]::Min($n, 63)  # ProcessorAffinity is 64-bit on Windows
      $mask = ([UInt64]1 -shl $bits) - [UInt64]1
      $p.ProcessorAffinity = [IntPtr]::new([Int64]$mask)
    }
  } catch {}
}

function Find-LatestPerfCsv([string]$workDir, [datetime]$start) {
  Get-ChildItem -LiteralPath $workDir -Filter "performance_report_*.csv" -File -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $start.AddSeconds(-1) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
}

function Parse-CaseSizesFromLog([string]$logPath) {
  $text = Get-Content -Path $logPath -Raw -Encoding Default
  $map = @{}
  $rx = [regex]'(?m)^TestCase_(\d+)\s+.*?\s+(\d+)\*(\d+)\s+'
  foreach ($m in $rx.Matches($text)) {
    $id = [int]$m.Groups[1].Value
    $n = [int]$m.Groups[2].Value
    $map[$id] = $n
  }
  return $map
}

function Read-PerfCsv([string]$csvPath) {
  $rows = Import-Csv -LiteralPath $csvPath
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
  foreach ($id in 1..15) {
    $n = [int]$caseSizes[$id]
    $w = Case-Weight $n
    $s += $w * [double]$perfByCase[$id].total_ms
  }
  return $s
}

function Run-One([string]$name, [string]$root, [int]$algChoice, [string]$outDir) {
  $exe = Join-Path $root "x64\\Release\\SparseMatrixLUTest.exe"
  $workDir = Join-Path $root "SparseMatrixLUTest"
  if (-not (Test-Path $exe)) { throw "Missing exe: $exe" }
  if (-not (Test-Path $workDir)) { throw "Missing work dir: $workDir" }

  $ts = Get-Date -Format "yyyyMMdd_HHmmss_fff"
  $stdin = Join-Path $outDir ("{0}_alg{1}_{2}.stdin.txt" -f $name, $algChoice, $ts)
  $stdout = Join-Path $outDir ("{0}_alg{1}_{2}.out.txt" -f $name, $algChoice, $ts)
  $stderr = Join-Path $outDir ("{0}_alg{1}_{2}.err.txt" -f $name, $algChoice, $ts)

  (($algChoice.ToString()) + "`r`n0`r`n") | Set-Content -NoNewline -Encoding ASCII $stdin

  $start = Get-Date
  $p = Start-Process -FilePath $exe -WorkingDirectory $workDir `
    -RedirectStandardInput $stdin -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru

  Set-HighPerfProcess $p
  $p.WaitForExit()

  $perf = Find-LatestPerfCsv $workDir $start
  if (-not $perf) { throw "Missing performance_report_*.csv for $name alg=$algChoice" }

  $csvOut = Join-Path $outDir ("{0}_alg{1}_{2}.{3}" -f $name, $algChoice, $ts, $perf.Name)
  Copy-Item -Force $perf.FullName $csvOut

  [pscustomobject]@{
    name = $name
    alg = $algChoice
    csv = $csvOut
    log = $stdout
    err = $stderr
  }
}

function ABAB-Full15([string]$name, [string]$root, [int]$candidateAlgChoice) {
  $bWeighted=@(); $cWeighted=@()
  $bPerf=@(); $cPerf=@()
  $caseSizes = $null

  for ($i=1; $i -le $Reps; $i++) {
    Write-Host ("[{0}] baseline rep {1}/{2}" -f $name, $i, $Reps)
    $b = Run-One $name $root 0 $OutDir
    if (-not $caseSizes) {
      $caseSizes = Parse-CaseSizesFromLog $b.log
      if (-not $caseSizes -or $caseSizes.Count -lt 15) {
        throw "Failed to parse 15 case sizes from log: $($b.log) (parsed=$($caseSizes.Count))"
      }
    }
    $bp = Read-PerfCsv $b.csv
    $bPerf += $bp
    $bWeighted += (Weighted-Total $caseSizes $bp)

    Write-Host ("[{0}] candidate rep {1}/{2}" -f $name, $i, $Reps)
    $c = Run-One $name $root $candidateAlgChoice $OutDir
    $cp = Read-PerfCsv $c.csv
    $cPerf += $cp
    $cWeighted += (Weighted-Total $caseSizes $cp)
  }

  $wbMed = Median ([double[]]$bWeighted)
  $wcMed = Median ([double[]]$cWeighted)
  $speedup = if ($wcMed -gt 0) { $wbMed / $wcMed } else { [double]::NaN }

  $caseRows=@()
  for ($id=1; $id -le 15; $id++) {
    $n = [int]$caseSizes[$id]
    $w = Case-Weight $n

    $bTotal = Median ([double[]]($bPerf | ForEach-Object { $_[$id].total_ms }))
    $bRearr = Median ([double[]]($bPerf | ForEach-Object { $_[$id].rearr_ms }))
    $bDecomp = Median ([double[]]($bPerf | ForEach-Object { $_[$id].decomp_ms }))

    $cTotal = Median ([double[]]($cPerf | ForEach-Object { $_[$id].total_ms }))
    $cRearr = Median ([double[]]($cPerf | ForEach-Object { $_[$id].rearr_ms }))
    $cDecomp = Median ([double[]]($cPerf | ForEach-Object { $_[$id].decomp_ms }))

    $caseRows += [pscustomobject]@{
      TestCase = "TestCase_$id"
      N = $n
      Weight = $w
      BaselineTotalMs = $bTotal
      CandidateTotalMs = $cTotal
      BaselineRearrMs = $bRearr
      BaselineDecompMs = $bDecomp
      CandidateRearrMs = $cRearr
      CandidateDecompMs = $cDecomp
    }
  }

  [pscustomobject]@{
    name = $name
    root = $root
    baseline_weighted_median_ms = $wbMed
    candidate_weighted_median_ms = $wcMed
    speedup_baseline_over_candidate = $speedup
    case_times_median = $caseRows
  }
}

# Clean any leftover env that could bias results.
Get-ChildItem Env:USERALGO_* -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue

$repo = Resolve-RepoRoot
if (-not $OursRoot) { $OursRoot = Join-Path $repo "SparseMatrixLUTestFinal" }
if (-not $TheirsRoot) { throw "Provide -TheirsRoot for the reference implementation." }
$OursRoot = (Resolve-Path $OursRoot).Path
$TheirsRoot = (Resolve-Path $TheirsRoot).Path

if (-not $OutDir) {
  $ts = Get-Date -Format "yyyyMMdd_HHmmss"
  $OutDir = Join-Path $repo ("build\\full15_abab_{0}" -f $ts)
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host ("[full15_abab] Reps={0}" -f $Reps)
Write-Host ("[full15_abab] ours   = {0}" -f $OursRoot)
Write-Host ("[full15_abab] theirs = {0}" -f $TheirsRoot)
Write-Host ("[full15_abab] outdir = {0}" -f $OutDir)

# our harness: 0 baseline (SuiteSparse), 1 candidate (UserAlgorithm)
$ours = ABAB-Full15 "ours" $OursRoot 1

# system-team harness: 0 baseline (SuiteSparse), 1 candidate (BTFEigen)
$theirs = ABAB-Full15 "theirs" $TheirsRoot 1

$summary = [pscustomobject]@{
  reps = $Reps
  ours_baseline_weighted_median_ms = $ours.baseline_weighted_median_ms
  ours_candidate_weighted_median_ms = $ours.candidate_weighted_median_ms
  ours_speedup_baseline_over_candidate = $ours.speedup_baseline_over_candidate
  theirs_baseline_weighted_median_ms = $theirs.baseline_weighted_median_ms
  theirs_candidate_weighted_median_ms = $theirs.candidate_weighted_median_ms
  theirs_speedup_baseline_over_candidate = $theirs.speedup_baseline_over_candidate
  ours_over_theirs_candidate = if ($theirs.candidate_weighted_median_ms -gt 0) { $ours.candidate_weighted_median_ms / $theirs.candidate_weighted_median_ms } else { [double]::NaN }
}

$summary | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "compare_full15_abab_summary.csv")
$ours.case_times_median | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "case_times_median_ours.csv")
$theirs.case_times_median | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "case_times_median_theirs.csv")

# Join per-case candidate totals for quick comparison
$join=@()
for ($id=1; $id -le 15; $id++) {
  $tc = "TestCase_$id"
  $a = $ours.case_times_median | Where-Object { $_.TestCase -eq $tc } | Select-Object -First 1
  $b = $theirs.case_times_median | Where-Object { $_.TestCase -eq $tc } | Select-Object -First 1
  $join += [pscustomobject]@{
    TestCase = $tc
    N = if ($a) { [int]$a.N } else { 0 }
    Weight = if ($a) { [double]$a.Weight } else { [double]::NaN }
    OursCandidateMs = if ($a) { [double]$a.CandidateTotalMs } else { [double]::NaN }
    TheirsCandidateMs = if ($b) { [double]$b.CandidateTotalMs } else { [double]::NaN }
    OursOverTheirs = if ($a -and $b -and [double]$b.CandidateTotalMs -gt 0) { [double]$a.CandidateTotalMs / [double]$b.CandidateTotalMs } else { [double]::NaN }
    DeltaMs_OursMinusTheirs = if ($a -and $b) { [double]$a.CandidateTotalMs - [double]$b.CandidateTotalMs } else { [double]::NaN }
  }
}
$join | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "case_times_median_ours_vs_theirs.csv")

Write-Host ""
Write-Host "=== Weighted (median) ==="
$summary | Format-List
Write-Host ("Outputs: {0}" -f $OutDir)

[pscustomobject]@{
  out_dir = $OutDir
  summary_csv = (Join-Path $OutDir "compare_full15_abab_summary.csv")
  ours_cases_csv = (Join-Path $OutDir "case_times_median_ours.csv")
  theirs_cases_csv = (Join-Path $OutDir "case_times_median_theirs.csv")
  join_csv = (Join-Path $OutDir "case_times_median_ours_vs_theirs.csv")
}
