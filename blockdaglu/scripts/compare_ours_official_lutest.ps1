param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [ValidateRange(1, 9)]
  [int]$Reps = 3,

  [Parameter(Mandatory = $false)]
  [int]$Threads = 0,

  [Parameter(Mandatory = $false)]
  [ValidateSet("default", "auto", "klu")]
  [string]$Mode = "auto"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot { (Resolve-Path (Join-Path $PSScriptRoot "..\\..")).Path }

function Sync-CandidateDll([string]$config) {
  $root = Resolve-RepoRoot
  $src = Join-Path $root ("build\\user_algorithm\\{0}\\useralgo_baseline.dll" -f $config)
  $dstDir = Join-Path $root ("SparseMatrixLUTest\\x64\\{0}" -f $config)
  $dst = Join-Path $dstDir "useralgo_baseline.dll"

  if (-not (Test-Path $src)) {
    Write-Host ("WARN: missing candidate DLL: {0}" -f $src)
    return
  }
  if (-not (Test-Path $dstDir)) {
    Write-Host ("WARN: missing SparseMatrixLUTest dll dir: {0}" -f $dstDir)
    return
  }

  $needCopy = $true
  if (Test-Path $dst) {
    $a = Get-Item $src
    $b = Get-Item $dst
    if ($a.Length -eq $b.Length -and $a.LastWriteTime -le $b.LastWriteTime) { $needCopy = $false }
  }
  if ($needCopy) {
    Copy-Item -Force $src $dst
    Write-Host ("Synced candidate DLL: {0} -> {1}" -f $src, $dst)
  }
}

function Weighted-Total([string]$csvPath) {
  $rows = Import-Csv -Path $csvPath
  if (-not $rows -or $rows.Count -lt 7) { return [double]::NaN }
  $weights = @{ 1 = 0.1; 2 = 0.1; 3 = 0.1; 4 = 0.1; 5 = 0.15; 6 = 0.25; 7 = 0.5 }
  $s = 0.0
  foreach ($r in $rows) {
    $id = [int](($r.TestCase) -replace "TestCase_", "")
    $s += [double]$r."TotalTime(ms)" * $weights[$id]
  }
  return $s
}

function Parse-XRelErrors([string]$logPath) {
  $lines = Get-Content -Path $logPath
  $errs = @()
  $pattern = [regex]"\|\|x\s*-\s*x\*\|\|\s*/\s*\|\|x\*\|\|:\s*([0-9eE\+\-\.]+)"
  foreach ($line in $lines) {
    $m = $pattern.Match($line)
    if ($m.Success) {
      $errs += [double]::Parse($m.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    }
  }
  if ($errs.Count -ge 7) { return $errs[0..6] }

  $byCase = @{}
  foreach ($line in $lines) {
    if ($line -match '^TestCase_(\d+)\s+') {
      $id = [int]$matches[1]
      if ($line -match '\s([0-9eE\+\-\.]+)\s*$') {
        $byCase[$id] = [double]::Parse($matches[1], [Globalization.CultureInfo]::InvariantCulture)
      }
    }
  }
  if ($byCase.Count -ge 7) { return 1..7 | ForEach-Object { $byCase[$_] } }
  return @()
}

function Median([double[]]$xs) {
  if ($null -eq $xs -or $xs.Count -eq 0) { return [double]::NaN }
  $ys = $xs | Sort-Object
  $n = $ys.Count
  if (($n % 2) -eq 1) { return [double]$ys[($n - 1) / 2] }
  return 0.5 * ([double]$ys[$n / 2 - 1] + [double]$ys[$n / 2])
}

function Run-One([string]$outDir, [string]$tag, [int]$algChoice) {
  $root = Resolve-RepoRoot
  $exe = Join-Path $root ("SparseMatrixLUTest\\x64\\{0}\\SparseMatrixLUTest.exe" -f $Config)
  if (-not (Test-Path $exe)) { throw "Missing exe: $exe" }

  $dllDir = Join-Path $root ("SparseMatrixLUTest\\x64\\{0}" -f $Config)
  $workDir = Join-Path $root "SparseMatrixLUTest\\SparseMatrixLUTest"

  $t = Get-Date -Format "yyyyMMdd_HHmmss_fff"
  $stdin = Join-Path $outDir ("{0}_{1}.stdin.txt" -f $tag, $t)
  (($algChoice.ToString()) + "`r`n0`r`n" + (("0`r`n") * 12)) | Set-Content -NoNewline -Encoding ASCII $stdin

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

$root = Resolve-RepoRoot
$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $root ("build\\ours_official_compare_{0}_{1}" -f $ts, $Mode)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("OutDir:  {0}" -f $outDir)
Write-Host ("Config:  {0}  Reps: {1}  Mode: {2}  Threads: {3}" -f $Config, $Reps, $Mode, $Threads)

# Ensure SparseMatrixLUTest picks up the latest built DLL.
Sync-CandidateDll $Config

# Candidate env
Remove-Item Env:USERALGO_MODE -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_THREADS -ErrorAction SilentlyContinue
if ($Threads -gt 0) { $env:USERALGO_THREADS = "$Threads" }
if ($Mode -ne "default") { $env:USERALGO_MODE = $Mode }
Remove-Item Env:USERALGO_PROFILE -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_REFINE_ITERS -ErrorAction SilentlyContinue
$env:USERALGO_PROFILE = "0"
$env:USERALGO_REFINE_ITERS = "0"

$wb = @()
$wc = @()
$eps = $null
$err = $null

for ($i = 1; $i -le $Reps; $i++) {
  # ABAB: alternate baseline and candidate to reduce drift from thermals/background load.
  Write-Host ("baseline rep {0}/{1}..." -f $i, $Reps)
  $b = Run-One $outDir ("baseline_r{0}" -f $i) 0
  if ($b.csv) { $wb += (Weighted-Total $b.csv) }
  if (-not $eps) { $e = Parse-XRelErrors $b.log; if ($e.Count -ge 7) { $eps = $e } }

  Write-Host ("candidate rep {0}/{1}..." -f $i, $Reps)
  $c = Run-One $outDir ("candidate_r{0}" -f $i) 1
  if ($c.csv) { $wc += (Weighted-Total $c.csv) }
  if (-not $err) { $e = Parse-XRelErrors $c.log; if ($e.Count -ge 7) { $err = $e } }
}

$pass = $null
$passCount = 0
$minMargin = [double]::NaN
if ($eps -and $err -and $eps.Count -ge 7 -and $err.Count -ge 7) {
  $pass = $true
  $minMargin = [double]::PositiveInfinity
  for ($i = 0; $i -lt 7; $i++) {
    $m = $eps[$i] - $err[$i]
    if ($m -lt $minMargin) { $minMargin = $m }
    if ($err[$i] -le $eps[$i]) { $passCount++ } else { $pass = $false }
  }
}

$wbMed = Median ([double[]]$wb)
$wcMed = Median ([double[]]$wc)
$speedup = if ([double]::IsNaN($wbMed) -or [double]::IsNaN($wcMed) -or $wcMed -eq 0.0) { [double]::NaN } else { $wbMed / $wcMed }

$summary = [pscustomobject]@{
  reps = $Reps
  mode = $Mode
  threads = $Threads
  pass_7of7_err_le_baseline = $pass
  pass_count = $passCount
  min_margin_eps_minus_user = $minMargin
  weighted_baseline_median_ms = $wbMed
  weighted_candidate_median_ms = $wcMed
  speedup_baseline_over_candidate = $speedup
}

$summaryPath = Join-Path $outDir "summary.csv"
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath

Write-Host ("Summary: {0}" -f $summaryPath)
