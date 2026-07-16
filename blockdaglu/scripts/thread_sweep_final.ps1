param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [ValidateSet("auto", "klu", "block", "default")]
  [string]$Mode = "auto",

  [Parameter(Mandatory = $false)]
  [int]$Reps = 3,

  [Parameter(Mandatory = $false)]
  [int[]]$ThreadsList = @(1, 2, 4, 6, 8, 10, 12, 14, 16),

  [Parameter(Mandatory = $false)]
  [string]$FinalRoot = "",

  [Parameter(Mandatory = $false)]
  [string]$CandidateDll = "",

  [Parameter(Mandatory = $false)]
  [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot { (Resolve-Path (Join-Path $PSScriptRoot "..\\..")).Path }

$repo = Resolve-RepoRoot

if (-not $FinalRoot) { $FinalRoot = Join-Path $repo "SparseMatrixLUTest_Final" }
$FinalRoot = (Resolve-Path $FinalRoot).Path

if (-not $CandidateDll) {
  $CandidateDll = Join-Path $repo ("build\\user_algorithm\\{0}\\useralgo_baseline.dll" -f $Config)
}
if (-not (Test-Path $CandidateDll)) {
  throw "Missing CandidateDll: $CandidateDll (build it first: cmake --build build/user_algorithm --config $Config)"
}

if (-not $OutDir) {
  $OutDir = Join-Path $repo "docs\\performance\\20260107"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$detailsRows = @()

foreach ($t in $ThreadsList) {
  Write-Host ("[thread_sweep_final] threads={0}, reps={1}, mode={2}" -f $t, $Reps, $Mode)

  $run = & (Join-Path $PSScriptRoot "compare_lutest_final_single.ps1") `
    -Config $Config -Reps $Reps -Threads $t -Mode $Mode -FinalRoot $FinalRoot `
    -CandidateDll $CandidateDll -Tag ("threads{0}" -f $t)

  $cases = Import-Csv -Path $run.case_times_csv
  foreach ($r in $cases) {
    $bTotal = [double]$r.BaselineTotalMs
    $cTotal = [double]$r.CandidateTotalMs
    $speedup = if ($cTotal -eq 0.0) { [double]::NaN } else { $bTotal / $cTotal }

    $detailsRows += [pscustomobject]@{
      Threads = $t
      TestCase = $r.TestCase
      Baseline_Total_ms = $bTotal
      User_Total_ms = $cTotal
      Speedup_x = $speedup
      Baseline_Reorder_ms = [double]$r.BaselineRearrMs
      User_Reorder_ms = [double]$r.CandidateRearrMs
      Baseline_Decomp_ms = [double]$r.BaselineDecompMs
      User_Decomp_ms = [double]$r.CandidateDecompMs
      Baseline_Prep_ms = [double]$r.BaselinePrepMs
      User_Prep_ms = [double]$r.CandidatePrepMs
    }
  }
}

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$detailsCsv = Join-Path $OutDir ("threads_sweep_details_final_{0}.csv" -f $ts)
$detailsRows | Export-Csv -NoTypeInformation -Encoding UTF8 $detailsCsv

Write-Host ("[thread_sweep_final] Wrote: {0}" -f $detailsCsv)

# Generate figures/csv summaries.
$plot = Join-Path $repo "docs\\performance\\20260106\\thread_sweep_plot.py"
python $plot --details $detailsCsv --out-dir $OutDir

# Copy the two paper-friendly figures into the XeLaTeX fig folder.
# Avoid hardcoding non-ASCII paths (PowerShell 5.1 may parse scripts as ANSI without BOM).
  $paper = Get-ChildItem -Path $repo -Recurse -File -Filter "paper.tex" -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($paper) {
    $figDir = Join-Path $paper.Directory.FullName "fig"
    if (Test-Path $figDir) {
      Copy-Item -Force (Join-Path $OutDir "fig_thread_sweep_overall.png") $figDir
    Copy-Item -Force (Join-Path $OutDir "fig_thread_sweep_heatmap.png") $figDir
      Write-Host ("[thread_sweep_final] Copied to: {0}" -f $figDir)
    } else {
      Write-Warning ("[thread_sweep_final] Skip copy: fig dir not found: {0}" -f $figDir)
    }
  } else {
  Write-Warning "[thread_sweep_final] Skip copy: cannot find paper.tex under repo root."
}
