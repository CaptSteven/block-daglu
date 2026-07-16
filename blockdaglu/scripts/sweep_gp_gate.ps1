param(
  [Parameter(Mandatory = $false)]
  [string]$CasesPath = "SparseMatrixLUTest\\PreliminaryTestCases",

  [Parameter(Mandatory = $false)]
  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [int]$Threads = 0,

  [Parameter(Mandatory = $false)]
  [int]$Runs = 3,

  [Parameter(Mandatory = $false)]
  [int]$GpMaxN = 4096,

  [Parameter(Mandatory = $false)]
  [string]$SwapMaxNs = "128,256,512",

  [Parameter(Mandatory = $false)]
  [string]$SwapMaxs = "0,1,2,4"
)

$ErrorActionPreference = "Continue"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\\..")
$cases = Resolve-Path (Join-Path $root $CasesPath)

$exe = Join-Path $root ("build\\user_algorithm\\{0}\\useralgo_benchmark.exe" -f $Config)
if (-not (Test-Path $exe)) { throw "Missing exe: $exe" }

$dllDir = Join-Path $root "SparseMatrixLUTest\\x64\\Release"
if (-not (Test-Path $dllDir)) { throw "Missing DLL dir: $dllDir" }
$env:PATH = "$dllDir;$env:PATH"

if ($Threads -gt 0) { $env:USERALGO_THREADS = "$Threads" } else { Remove-Item Env:USERALGO_THREADS -ErrorAction SilentlyContinue }

# Use AUTO by default (so small cases remain stable).
Remove-Item Env:USERALGO_MODE -ErrorAction SilentlyContinue
$env:USERALGO_BLOCK_TOPK = "0"
$env:USERALGO_SUPERBLOCK_STATS = "0"
$env:USERALGO_SUPERBLOCK_BBD = "0"
$env:USERALGO_SUPERBLOCK_SUBDECOMP = "0"
$env:USERALGO_REFINE_ITERS = "0"
$env:USERALGO_GP_GATE_LOG = "1"

$env:USERALGO_SPARSE_LU_KERNEL = "gp"
$env:USERALGO_GP_MAX_N = "$GpMaxN"

function Parse-GpGate([string]$text) {
  $line = ([regex]::Split($text, '\r?\n') | Where-Object { $_ -like '*USERALGO_PROFILE,gp_gate,*' } | Select-Object -Last 1)
  if (-not $line) { return $null }
  $idx = $line.IndexOf('USERALGO_PROFILE,gp_gate,')
  if ($idx -ge 0) { $line = $line.Substring($idx) }
  $kv = @{}
  foreach ($p in $line.Split(',')) {
    if ($p -notlike '*=*') { continue }
    $parts = $p.Split('=', 2)
    $kv[$parts[0]] = $parts[1]
  }
  return $kv
}

function Parse-Weighted([string]$text) {
  $line = ([regex]::Split($text, '\r?\n') | Where-Object { $_ -like 'UserAlgorithm_weighted_total_ms,*' } | Select-Object -First 1)
  if (-not $line) { return $null }
  return [double]($line.Split(',')[1])
}

function Parse-Pass([string]$text) {
  # If any per-case pass==0, treat as fail.
  $lines = [regex]::Split($text, '\r?\n')
  $hdr = ($lines | Where-Object { $_ -eq 'case,n,nnz,xstar_rel_residual,ss_rel_residual,ua_rel_residual,eps_suitesparse,err_user,margin_eps_minus_user,pass_user_le_eps' } | Select-Object -First 1)
  if (-not $hdr) { return $null }
  $bad = ($lines | Where-Object { $_ -match '^[1-7],' } | Where-Object { $_.Split(',')[-1] -eq '0' } | Measure-Object).Count
  return ($bad -eq 0)
}

$swapMaxNList = @()
foreach ($x in ($SwapMaxNs -split ',')) { $t=$x.Trim(); if ($t -ne '') { $swapMaxNList += [int]$t } }
if ($swapMaxNList.Count -eq 0) { $swapMaxNList = @(256) }

$swapMaxList = @()
foreach ($x in ($SwapMaxs -split ',')) { $t=$x.Trim(); if ($t -ne '') { $swapMaxList += [int]$t } }
if ($swapMaxList.Count -eq 0) { $swapMaxList = @(0,1,2) }

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $root ("build\\sweep_gp_gate_{0}" -f $ts)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$csv = Join-Path $outDir "sweep.csv"
[System.IO.File]::WriteAllText(
  $csv,
  "gp_max_n,swap_max_n,swap_max,runs,weighted_total_ms_median,pass_median,gp_tried,gp_used,rej_fail,rej_swaps,rej_tiny,rej_mindiag,max_used_n,log_info,log_perf`r`n",
  [System.Text.UTF8Encoding]::new($false)
)

foreach ($swapMaxN in $swapMaxNList) {
  foreach ($swapMax in $swapMaxList) {
    $env:USERALGO_GP_REQUIRE_NO_SWAPS = "1"
    $env:USERALGO_GP_SWAP_MAX_N = "$swapMaxN"
    $env:USERALGO_GP_SWAP_MAX = "$swapMax"

    $totals = @()
    $passes = @()
    $lastGate = $null
    $logProfile = Join-Path $outDir ("gpN{0}_swapN{1}_swap{2}_profile.txt" -f $GpMaxN,$swapMaxN,$swapMax)
    $logPerf = Join-Path $outDir ("gpN{0}_swapN{1}_swap{2}_perf.txt" -f $GpMaxN,$swapMaxN,$swapMax)

    # One info run (profiling disabled): collect gp_gate stats via USERALGO_GP_GATE_LOG=1.
    $env:USERALGO_PROFILE = "0"
    $text0 = (& $exe $cases 2>&1 | Out-String)
    [System.IO.File]::WriteAllText($logProfile, $text0, [System.Text.Encoding]::Unicode)
    $lastGate = Parse-GpGate $text0

    # Multiple perf runs with profiling disabled: measure weighted_total_ms median + pass stability.
    for ($i=1; $i -le $Runs; $i++) {
      $text = (& $exe $cases 2>&1 | Out-String)
      # append logs (unicode) for later inspection
      [System.IO.File]::AppendAllText($logPerf, ("===== RUN {0} =====`r`n" -f $i), [System.Text.Encoding]::Unicode)
      [System.IO.File]::AppendAllText($logPerf, $text, [System.Text.Encoding]::Unicode)
      $w = Parse-Weighted $text
      if ($w -ne $null) { $totals += $w }
      $p = Parse-Pass $text
      if ($p -ne $null) { $passes += $p }
    }

    $wmed = [double]::NaN
    if ($totals.Count -gt 0) {
      $sorted = $totals | Sort-Object
      $mid = [int][math]::Floor(($sorted.Count - 1) / 2)
      $wmed = [double]$sorted[$mid]
    }
    $passAll = $false
    if ($passes.Count -gt 0) {
      $badCount = ($passes | Where-Object { $_ -eq $false } | Measure-Object).Count
      $passAll = ($badCount -eq 0)
    }

    $gp_tried = if ($lastGate -and $lastGate.ContainsKey('tried')) { $lastGate['tried'] } else { '' }
    $gp_used = if ($lastGate -and $lastGate.ContainsKey('used')) { $lastGate['used'] } else { '' }
    $rej_fail = if ($lastGate -and $lastGate.ContainsKey('rej_fail')) { $lastGate['rej_fail'] } else { '' }
    $rej_swaps = if ($lastGate -and $lastGate.ContainsKey('rej_swaps')) { $lastGate['rej_swaps'] } else { '' }
    $rej_tiny = if ($lastGate -and $lastGate.ContainsKey('rej_tiny')) { $lastGate['rej_tiny'] } else { '' }
    $rej_mindiag = if ($lastGate -and $lastGate.ContainsKey('rej_mindiag')) { $lastGate['rej_mindiag'] } else { '' }
    $max_used_n = if ($lastGate -and $lastGate.ContainsKey('max_used_n')) { $lastGate['max_used_n'] } else { '' }

    $passInt = if ($passAll) { 1 } else { 0 }
    $row = "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14}" -f `
      $GpMaxN,$swapMaxN,$swapMax,$Runs, `
      $wmed,$passInt, `
      $gp_tried,$gp_used,$rej_fail,$rej_swaps,$rej_tiny,$rej_mindiag,$max_used_n, `
      (Split-Path -Leaf $logProfile), `
      (Split-Path -Leaf $logPerf)
    [System.IO.File]::AppendAllText($csv, ($row + "`r`n"), [System.Text.UTF8Encoding]::new($false))
    Write-Host ("gp_max_n={0} swap_max_n={1} swap_max={2} pass={3} weighted={4} gp_used={5}" -f $GpMaxN,$swapMaxN,$swapMax,$passInt,$wmed,$gp_used)
  }
}

Write-Host ("Done. CSV: {0}" -f $csv)
Write-Host ("Logs: {0}" -f $outDir)
