param(
  [Parameter(Mandatory = $false)]
  [int]$CaseId = 12,

  [Parameter(Mandatory = $false)]
  [ValidateRange(1, 9)]
  [int]$Reps = 3,

  [Parameter(Mandatory = $false)]
  [int]$EigenMinN = 20000,

  [Parameter(Mandatory = $false)]
  [string]$Root = "",

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

function ABAB-For([string]$name, [string]$root, [int]$caseId, [int]$reps) {
  $b_total=@(); $c_total=@()
  $b_p=@(); $b_r=@(); $b_d=@()
  $c_p=@(); $c_r=@(); $c_d=@()
  $b_nnz=@(); $c_nnz=@()
  $b_lu=@(); $c_lu=@()

  for ($i=1; $i -le $reps; $i++) {
    Write-Host ("[{0}] baseline rep {1}/{2}" -f $name, $i, $reps)
    $b = Run-One $name $root 0 $caseId $OutDir
    $b_total += $b.total_ms; $b_p += $b.prep_ms; $b_r += $b.rearr_ms; $b_d += $b.decomp_ms
    $b_nnz += $b.rearr_nnz; $b_lu += ($b.l_nnz + $b.u_nnz)

    Write-Host ("[{0}] candidate rep {1}/{2}" -f $name, $i, $reps)
    $c = Run-One $name $root 1 $caseId $OutDir
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

$repo = Resolve-RepoRoot
if (-not $Root) { $Root = Join-Path $repo "SparseMatrixLUTestFinal" }
$Root = (Resolve-Path $Root).Path

if (-not $OutDir) {
  $ts = Get-Date -Format "yyyyMMdd_HHmmss"
  $OutDir = Join-Path $repo ("build\\case{0}_abab_ours_eigen_{1}" -f $CaseId, $ts)
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host ("[case_abab_ours_eigen] Root={0}" -f $Root)
Write-Host ("[case_abab_ours_eigen] CaseId={0} Reps={1}" -f $CaseId, $Reps)
Write-Host ("[case_abab_ours_eigen] EigenMinN={0}" -f $EigenMinN)
Write-Host ("[case_abab_ours_eigen] OutDir={0}" -f $OutDir)

$savedEigen = $env:USERALGO_EIGEN
$savedEigenMin = $env:USERALGO_EIGEN_MIN_N

try {
  # Variant A: Eigen OFF
  $env:USERALGO_EIGEN = "0"
  $env:USERALGO_EIGEN_MIN_N = "$EigenMinN"
  Write-Host ""
  Write-Host "=== Variant A: USERALGO_EIGEN=0 ==="
  $a = ABAB-For "eigen_off" $Root $CaseId $Reps

  # Variant B: Eigen ON
  $env:USERALGO_EIGEN = "1"
  $env:USERALGO_EIGEN_MIN_N = "$EigenMinN"
  Write-Host ""
  Write-Host "=== Variant B: USERALGO_EIGEN=1 ==="
  $b = ABAB-For "eigen_on" $Root $CaseId $Reps

  $compare = [pscustomobject]@{
    case = $CaseId
    eigen_min_n = $EigenMinN
    candidate_total_median_ms_off = $a.candidate_total_median_ms
    candidate_total_median_ms_on = $b.candidate_total_median_ms
    speedup_off_over_on = if ($b.candidate_total_median_ms -gt 0) { $a.candidate_total_median_ms / $b.candidate_total_median_ms } else { [double]::NaN }
    delta_ms_on_minus_off = $b.candidate_total_median_ms - $a.candidate_total_median_ms
    candidate_decomp_median_ms_off = $a.candidate_decomp_median_ms
    candidate_decomp_median_ms_on = $b.candidate_decomp_median_ms
    delta_decomp_ms_on_minus_off = $b.candidate_decomp_median_ms - $a.candidate_decomp_median_ms
  }

  $a | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "variant_eigen_off_summary.csv")
  $b | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "variant_eigen_on_summary.csv")
  $compare | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "compare_eigen_off_on.csv")

  Write-Host ""
  Write-Host "=== Compare (candidate medians) ==="
  $compare | Format-List

  [pscustomobject]@{
    out_dir = $OutDir
    off_csv = (Join-Path $OutDir "variant_eigen_off_summary.csv")
    on_csv = (Join-Path $OutDir "variant_eigen_on_summary.csv")
    compare_csv = (Join-Path $OutDir "compare_eigen_off_on.csv")
  }
} finally {
  if ($null -eq $savedEigen) { Remove-Item Env:USERALGO_EIGEN -ErrorAction SilentlyContinue } else { $env:USERALGO_EIGEN = $savedEigen }
  if ($null -eq $savedEigenMin) { Remove-Item Env:USERALGO_EIGEN_MIN_N -ErrorAction SilentlyContinue } else { $env:USERALGO_EIGEN_MIN_N = $savedEigenMin }
}

