param(
  [Parameter(Mandatory = $false)]
  [string]$RefRoot = "",

  [Parameter(Mandatory = $false)]
  [string]$CasesPath = "SparseMatrixLUTest\\PreliminaryTestCases",

  [Parameter(Mandatory = $false)]
  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [ValidateSet("x64","Win32")]
  [string]$Platform = "x64"
)

$ErrorActionPreference = "Continue"

function Resolve-Root {
  return Resolve-Path (Join-Path $PSScriptRoot "..\\..")
}

function Detect-RefRoot([string]$root) {
  # Prefer a directory that contains many 32-hex-id subfolders (typical "reference projects" dump layout).
  $best = $null
  $bestScore = -1
  foreach ($d in (Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue)) {
    $subs = @(Get-ChildItem -Path $d.FullName -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
    $score = ($subs | Where-Object { $_ -match "^[0-9a-f]{32}" }).Count
    if ($score -gt $bestScore) { $bestScore = $score; $best = $d }
  }
  if ($best -and $bestScore -ge 3) { return $best.FullName }
  return $null
}

function Find-MSBuild {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\\Installer\\vswhere.exe"
  if (Test-Path $vswhere) {
    $msb = & $vswhere -latest -products * -find "MSBuild\\Current\\Bin\\MSBuild.exe"
    if ($msb -and (Test-Path $msb)) { return $msb }
  }
  $fallback = "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\MSBuild\\Current\\Bin\\MSBuild.exe"
  if (Test-Path $fallback) { return $fallback }
  throw "MSBuild not found (vswhere failed and fallback missing)."
}

function Ensure-Junction([string]$link, [string]$target) {
  if (Test-Path $link) { return }
  New-Item -ItemType Directory -Force (Split-Path $link -Parent) | Out-Null
  cmd /c "mklink /J \"$link\" \"$target\" >nul" | Out-Null
}

function Parse-XRelErrors([string]$logPath) {
  $errs = @()
  if (-not (Test-Path $logPath)) { return $errs }
  $pattern = [regex]"\|\|x - x\*\|\| / \|\|x\*\|\|:\s*([0-9eE\+\-\.]+)"
  foreach ($line in Get-Content $logPath) {
    $m = $pattern.Match($line)
    if ($m.Success) {
      $errs += [double]::Parse($m.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    }
  }
  return $errs
}

function Weighted-Total([string]$csvPath) {
  if (-not (Test-Path $csvPath)) { return [double]::NaN }
  $rows = Import-Csv $csvPath
  $weights = @{ 1 = 0.1; 2 = 0.1; 3 = 0.1; 4 = 0.1; 5 = 0.15; 6 = 0.25; 7 = 0.5 }
  $s = 0.0
  foreach ($r in $rows) {
    $id = [int](($r.TestCase) -replace "TestCase_", "")
    $w = $weights[$id]
    $s += [double]$r."TotalTime(ms)" * $w
  }
  return $s
}

function Run-SparseMatrixLUTest([string]$workDir, [string]$exePath, [string]$team, [string]$tag, [int]$algChoice, [string]$outDir) {
  Push-Location $workDir
  try {
    $t = Get-Date -Format "yyyyMMdd_HHmmss"
    $log = Join-Path $outDir ("{0}_{1}_{2}.log.txt" -f $team, $tag, $t)
    $before = Get-ChildItem -Filter "performance_report_*.csv" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName

    # Interactive inputs:
    #   1) algorithm: 0 SuiteSparse, 1 UserAlgorithm
    #   2) draw: 0 no
    cmd /c "(echo $algChoice&echo 0) | \"$exePath\" > \"$log\" 2>&1" | Out-Null
    Start-Sleep -Seconds 2

    $after = Get-ChildItem -Filter "performance_report_*.csv" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $after) { return @{ log = $log; csv = $null } }
    $dstCsv = Join-Path $outDir ("{0}_{1}_{2}.perf.csv" -f $team, $tag, $after.Name)
    Move-Item -Force $after.FullName $dstCsv
    return @{ log = $log; csv = $dstCsv }
  } finally {
    Pop-Location
  }
}

$root = Resolve-Root
$refRootDetected = $null
if ([string]::IsNullOrWhiteSpace($RefRoot)) {
  $refRootDetected = Detect-RefRoot $root
  if (-not $refRootDetected) { throw "RefRoot not provided and auto-detection failed. Pass -RefRoot <path>." }
  $refRootAbs = Resolve-Path $refRootDetected
} else {
  $refRootAbs = Resolve-Path (Join-Path $root $RefRoot)
}
$casesAbs = Resolve-Path (Join-Path $root $CasesPath)
$msbuild = Find-MSBuild

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $root ("build\\ref_compare_{0}" -f $ts)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("RefRoot: {0}" -f $refRootAbs)
Write-Host ("Cases:   {0}" -f $casesAbs)
Write-Host ("OutDir:  {0}" -f $outDir)

$summary = @()

Get-ChildItem $refRootAbs -Directory | ForEach-Object {
  $team = $_.Name
  $dir = $_.FullName

  $sln = Get-ChildItem -Path $dir -Recurse -Filter "SparseMatrixLUTest.sln" -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $sln) {
    $summary += [pscustomobject]@{
      team = $team
      status = "skip"
      reason = "no SparseMatrixLUTest.sln"
      build_ok = $false
      pass_7of7 = $null
      weighted_baseline = $null
      weighted_candidate = $null
      speedup_baseline_over_candidate = $null
      notes = ""
    }
    return
  }

  Write-Host ("== {0} ==" -f $team)
  Write-Host ("sln: {0}" -f $sln.FullName)

  $buildLog = Join-Path $outDir ("{0}_build.txt" -f $team)
  $buildOk = $null

  $slnDir = Split-Path $sln.FullName -Parent

  # Locate exe (prefer shipped Release binary; build only if missing).
  $exe = Get-ChildItem -Path $slnDir -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ieq "SparseMatrixLUTest.exe" -and $_.FullName -like "*\\x64\\Release\\*" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

  if (-not $exe) {
    # Build to obtain exe
    & $msbuild $sln.FullName /m /t:Build /p:Configuration=$Config /p:Platform=$Platform /v:m 2>&1 | Out-String | Set-Content -Encoding UTF8 $buildLog
    $buildOk = $LASTEXITCODE -eq 0

    $exe = Get-ChildItem -Path $slnDir -Recurse -File -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -ieq "SparseMatrixLUTest.exe" -and ($_.FullName -like "*\\x64\\Release\\*" -or $_.FullName -like "*\\x64\\Debug\\*" -or $_.FullName -like "*\\Release\\*") } |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 1
  } else {
    $buildOk = $true
  }

  if (-not $exe) {
    $summary += [pscustomobject]@{
      team = $team
      status = "fail"
      reason = "exe not found"
      build_ok = $buildOk
      pass_7of7 = $null
      weighted_baseline = $null
      weighted_candidate = $null
      speedup_baseline_over_candidate = $null
      notes = ("buildLog={0}" -f $buildLog)
    }
    return
  }

  # Determine work dir: use directory containing test.cpp, so that base_path := current_path().parent_path()/PreliminaryTestCases.
  $workDir = $null
  $testCpp = Get-ChildItem -Path $slnDir -Recurse -Filter "test.cpp" -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($testCpp) { $workDir = Split-Path $testCpp.FullName -Parent }
  if (-not $workDir -or -not (Test-Path $workDir)) {
    $workDir = $slnDir
  }

  # Ensure cases are visible at <workDir/../PreliminaryTestCases>
  $caseLink = Join-Path (Split-Path $workDir -Parent) "PreliminaryTestCases"
  if (-not (Test-Path $caseLink)) { Ensure-Junction $caseLink $casesAbs }

  # Run baseline (SuiteSparse) and candidate (UserAlgorithm)
  $b = Run-SparseMatrixLUTest $workDir $exe.FullName $team "baseline" 0 $outDir
  $c = Run-SparseMatrixLUTest $workDir $exe.FullName $team "candidate" 1 $outDir

  $eps = Parse-XRelErrors $b.log
  $err = Parse-XRelErrors $c.log

  $pass = $null
  $passCount = 0
  if ($eps.Count -ge 7 -and $err.Count -ge 7) {
    $pass = $true
    for ($i=0; $i -lt 7; $i++) {
      if ($err[$i] -le $eps[$i]) { $passCount++ } else { $pass = $false }
    }
  }

  $wb = if ($b.csv) { Weighted-Total $b.csv } else { [double]::NaN }
  $wc = if ($c.csv) { Weighted-Total $c.csv } else { [double]::NaN }
  $speedup = if ([double]::IsNaN($wb) -or [double]::IsNaN($wc) -or $wc -eq 0.0) { [double]::NaN } else { $wb / $wc }

  $summary += [pscustomobject]@{
    team = $team
    status = "ok"
    reason = ""
    build_ok = $buildOk
    pass_7of7 = $pass
    pass_count = $passCount
    weighted_baseline = $wb
    weighted_candidate = $wc
    speedup_baseline_over_candidate = $speedup
    notes = ("exe={0}; workDir={1}; buildLog={2}" -f $exe.FullName, $workDir, $buildLog)
  }
}

$summaryPath = Join-Path $outDir "summary.csv"
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath
Write-Host ("Summary: {0}" -f $summaryPath)
