param(
  [Parameter(Mandatory = $false)]
  [int]$CaseId = 12,

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

function Set-HighPerfProcess([System.Diagnostics.Process]$p) {
  try { $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::High } catch {}
  try {
    $n = [int]$env:NUMBER_OF_PROCESSORS
    if ($n -gt 0) {
      $bits = [Math]::Min($n, 63)  # ProcessorAffinity is 64-bit on Windows
      # PowerShell has no integer suffix literals like 1ULL; force UInt64 explicitly.
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

function Run-One([string]$name, [string]$root, [int]$algChoice, [int]$caseId, [string]$outDir) {
  $exe = Join-Path $root "x64\\Release\\SparseMatrixLUTest.exe"
  $workDir = Join-Path $root "SparseMatrixLUTest"
  if (-not (Test-Path $exe)) { throw "Missing exe: $exe" }
  if (-not (Test-Path $workDir)) { throw "Missing work dir: $workDir" }

  $ts = Get-Date -Format "yyyyMMdd_HHmmss_fff"
  $stdin = Join-Path $outDir ("{0}_alg{1}_case{2}_{3}.stdin.txt" -f $name, $algChoice, $caseId, $ts)
  $stdout = Join-Path $outDir ("{0}_alg{1}_case{2}_{3}.out.txt" -f $name, $algChoice, $caseId, $ts)
  $stderr = Join-Path $outDir ("{0}_alg{1}_case{2}_{3}.err.txt" -f $name, $algChoice, $caseId, $ts)

  (($algChoice.ToString()) + "`r`n0`r`n") | Set-Content -NoNewline -Encoding ASCII $stdin

  $start = Get-Date
  $p = Start-Process -FilePath $exe -ArgumentList @("$caseId") -WorkingDirectory $workDir `
    -RedirectStandardInput $stdin -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru

  Set-HighPerfProcess $p
  $p.WaitForExit()

  $perf = Find-LatestPerfCsv $workDir $start
  if (-not $perf) { throw "Missing performance_report_*.csv for $name alg=$algChoice case=$caseId" }

  $csvOut = Join-Path $outDir ("{0}_alg{1}_case{2}_{3}.{4}" -f $name, $algChoice, $caseId, $ts, $perf.Name)
  Copy-Item -Force $perf.FullName $csvOut

  $row = Import-Csv -LiteralPath $csvOut | Select-Object -First 1
  [pscustomobject]@{
    name = $name
    alg = $algChoice
    case = $caseId
    prep_ms = [double]$row."PreparationTime(ms)"
    rearr_ms = [double]$row."RearrangementTime(ms)"
    decomp_ms = [double]$row."DecompositionTime(ms)"
    total_ms = [double]$row."TotalTime(ms)"
    rearr_nnz = [double]$row."RearrangedNNZ"
    l_nnz = [double]$row."LNNZ"
    u_nnz = [double]$row."UNNZ"
    csv = $csvOut
    stdout = $stdout
    stderr = $stderr
  }
}

$repo = Resolve-RepoRoot
if (-not $OursRoot) { $OursRoot = Join-Path $repo "SparseMatrixLUTestFinal" }
if (-not $TheirsRoot) { throw "Provide -TheirsRoot for the reference implementation." }
$OursRoot = (Resolve-Path $OursRoot).Path
$TheirsRoot = (Resolve-Path $TheirsRoot).Path

if (-not $OutDir) {
  $ts = Get-Date -Format "yyyyMMdd_HHmmss"
  $OutDir = Join-Path $repo ("build\\case{0}_abab_{1}" -f $CaseId, $ts)
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host ("[case_abab] CaseId={0} Reps={1}" -f $CaseId, $Reps)
Write-Host ("[case_abab] ours   = {0}" -f $OursRoot)
Write-Host ("[case_abab] theirs = {0}" -f $TheirsRoot)
Write-Host ("[case_abab] outdir = {0}" -f $OutDir)

function ABAB-For([string]$name, [string]$root) {
  $b_total=@(); $c_total=@()
  $b_p=@(); $b_r=@(); $b_d=@()
  $c_p=@(); $c_r=@(); $c_d=@()
  $b_nnz=@(); $c_nnz=@()
  $b_lu=@(); $c_lu=@()

  for ($i=1; $i -le $Reps; $i++) {
    Write-Host ("[{0}] baseline rep {1}/{2}" -f $name, $i, $Reps)
    $b = Run-One $name $root 0 $CaseId $OutDir
    $b_total += $b.total_ms; $b_p += $b.prep_ms; $b_r += $b.rearr_ms; $b_d += $b.decomp_ms
    $b_nnz += $b.rearr_nnz; $b_lu += ($b.l_nnz + $b.u_nnz)

    Write-Host ("[{0}] candidate rep {1}/{2}" -f $name, $i, $Reps)
    $c = Run-One $name $root 1 $CaseId $OutDir
    $c_total += $c.total_ms; $c_p += $c.prep_ms; $c_r += $c.rearr_ms; $c_d += $c.decomp_ms
    $c_nnz += $c.rearr_nnz; $c_lu += ($c.l_nnz + $c.u_nnz)
  }

  $bMed = Median ([double[]]$b_total)
  $cMed = Median ([double[]]$c_total)
  [pscustomobject]@{
    name = $name
    baseline_total_median_ms = $bMed
    candidate_total_median_ms = $cMed
    speedup = if ($cMed -gt 0) { $bMed / $cMed } else { [double]::NaN }
    baseline_prep_median_ms = Median ([double[]]$b_p)
    baseline_rearr_median_ms = Median ([double[]]$b_r)
    baseline_decomp_median_ms = Median ([double[]]$b_d)
    candidate_prep_median_ms = Median ([double[]]$c_p)
    candidate_rearr_median_ms = Median ([double[]]$c_r)
    candidate_decomp_median_ms = Median ([double[]]$c_d)
    baseline_rearrnnz_median = Median ([double[]]$b_nnz)
    candidate_rearrnnz_median = Median ([double[]]$c_nnz)
    baseline_lu_nnz_median = Median ([double[]]$b_lu)
    candidate_lu_nnz_median = Median ([double[]]$c_lu)
  }
}

$ours = ABAB-For "ours" $OursRoot
$theirs = ABAB-For "theirs" $TheirsRoot

$compare = [pscustomobject]@{
  case = $CaseId
  ours_candidate_total_median_ms = $ours.candidate_total_median_ms
  theirs_candidate_total_median_ms = $theirs.candidate_total_median_ms
  ours_over_theirs = if ($theirs.candidate_total_median_ms -gt 0) { $ours.candidate_total_median_ms / $theirs.candidate_total_median_ms } else { [double]::NaN }
}

$ours | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "ours_case${CaseId}_abab_summary.csv")
$theirs | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "theirs_case${CaseId}_abab_summary.csv")
$compare | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "compare_case${CaseId}_abab_summary.csv")

Write-Host ""
Write-Host "=== ABAB median summary ==="
$ours | Format-List
$theirs | Format-List
$compare | Format-List
