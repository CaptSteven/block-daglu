param(
  [Parameter(Mandatory = $false)]
  # Keep this script ASCII-only so it runs under Windows PowerShell 5.x (which treats .ps1 as ANSI unless BOM).
  [string]$RefRoot = "",

  [Parameter(Mandatory = $false)]
  [string]$CasesPath = "SparseMatrixLUTest\\PreliminaryTestCases",

  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [ValidateSet("x64", "Win32")]
  [string]$Platform = "x64",

  [Parameter(Mandatory = $false)]
  [ValidateRange(1, 9)]
  [int]$Reps = 3,

  [Parameter(Mandatory = $false)]
  # Optional regex to select a subset of teams (matches team folder name under RefRoot).
  [string]$TeamRegex = ""
)

$ErrorActionPreference = "Continue"

function Resolve-RepoRoot {
  (Resolve-Path (Join-Path $PSScriptRoot "..\\..")).Path
}

function Ensure-Junction([string]$link, [string]$target) {
  if (Test-Path $link) { return }
  New-Item -ItemType Directory -Force (Split-Path $link -Parent) | Out-Null
  cmd.exe /d /c "mklink /J ""$link"" ""$target"" >nul" | Out-Null
}

function Parse-XRelErrors([string]$logPath) {
  if (-not (Test-Path $logPath)) { return @() }
  $lines = Get-Content -Path $logPath -ErrorAction SilentlyContinue

  # Prefer per-case print lines (they may include more digits than the final table).
  $errs = @()
  $pattern = [regex]"\|\|x\s*-\s*x\*\|\|\s*/\s*\|\|x\*\|\|:\s*([0-9eE\+\-\.]+)"
  foreach ($line in $lines) {
    $m = $pattern.Match($line)
    if ($m.Success) {
      $errs += [double]::Parse($m.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    }
  }
  if ($errs.Count -ge 7) { return $errs[0..6] }

  # Fallback: parse the final summary table.
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

  return $errs
}

function Weighted-Total([string]$csvPath) {
  if (-not (Test-Path $csvPath)) { return [double]::NaN }
  $rows = Import-Csv -Path $csvPath
  if (-not $rows -or $rows.Count -lt 7) { return [double]::NaN }
  $weights = @{ 1 = 0.1; 2 = 0.1; 3 = 0.1; 4 = 0.1; 5 = 0.15; 6 = 0.25; 7 = 0.5 }
  $s = 0.0
  foreach ($r in $rows) {
    $id = [int](($r.TestCase) -replace "TestCase_", "")
    $w = $weights[$id]
    $s += [double]$r."TotalTime(ms)" * $w
  }
  return $s
}

function Weighted-FromLog([string]$logPath) {
  if (-not (Test-Path $logPath)) { return [double]::NaN }
  $weights = @{ 1 = 0.1; 2 = 0.1; 3 = 0.1; 4 = 0.1; 5 = 0.15; 6 = 0.25; 7 = 0.5 }
  $byCase = @{}
  foreach ($line in Get-Content -Path $logPath -ErrorAction SilentlyContinue) {
    # Example row:
    # TestCase_7  934.9     2655.5    32.8      3623.3  61531*61531    ...
    if ($line -match '^TestCase_(\d+)\s+([0-9eE\+\-\.]+)\s+([0-9eE\+\-\.]+)\s+([0-9eE\+\-\.]+)\s+([0-9eE\+\-\.]+)\s+') {
      $id = [int]$matches[1]
      $total = [double]::Parse($matches[5], [Globalization.CultureInfo]::InvariantCulture)
      $byCase[$id] = $total
    }
  }
  if ($byCase.Count -lt 7) { return [double]::NaN }
  $sum = 0.0
  foreach ($id in 1..7) {
    $sum += $weights[$id] * $byCase[$id]
  }
  return $sum
}

function Median([double[]]$xs) {
  if ($null -eq $xs -or $xs.Count -eq 0) { return [double]::NaN }
  $ys = $xs | Sort-Object
  $n = $ys.Count
  if (($n % 2) -eq 1) { return [double]$ys[($n - 1) / 2] }
  return 0.5 * ([double]$ys[$n / 2 - 1] + [double]$ys[$n / 2])
}

function Find-ProjectRoot([string]$exePath, [string]$teamDir) {
  $dir = Split-Path -Parent $exePath
  for ($i = 0; $i -lt 10; $i++) {
    if (-not $dir.StartsWith($teamDir, [StringComparison]::OrdinalIgnoreCase)) { break }
    if (Test-Path (Join-Path $dir "SparseMatrixLUTest.sln")) { return $dir }
    if (Test-Path (Join-Path $dir "SparseMatrixLUTest")) { return $dir }
    $next = Split-Path -Parent $dir
    if (-not $next -or $next -eq $dir) { break }
    $dir = $next
  }
  return (Split-Path -Parent $exePath)
}

function Get-TeamName([string]$refRootAbs, [string]$exeFullPath) {
  if ($exeFullPath.StartsWith($refRootAbs, [StringComparison]::OrdinalIgnoreCase)) {
    $rel = $exeFullPath.Substring($refRootAbs.Length).TrimStart("\", "/")
    return ($rel.Split(@("\", "/"))[0])
  }
  return (Split-Path -Leaf (Split-Path -Parent $exeFullPath))
}

function Team-Key([string]$team) {
  # Stable short id to avoid MAX_PATH issues when team names are very long.
  $sha1 = [System.Security.Cryptography.SHA1]::Create()
  try {
    $bytes = [Text.Encoding]::UTF8.GetBytes($team)
    $hashBytes = $sha1.ComputeHash($bytes)
    (($hashBytes | ForEach-Object { $_.ToString("x2") }) -join "").Substring(0, 12)
  } finally {
    $sha1.Dispose()
  }
}

function Invoke-LUTest([string]$team, [string]$tag, [string]$workDir, [string]$exePath, [int]$algChoice, [string]$outDir) {
  $teamKey = Team-Key $team
  $teamDir = Join-Path $outDir $teamKey
  New-Item -ItemType Directory -Force -Path $teamDir | Out-Null

  $t = Get-Date -Format "yyyyMMdd_HHmmss_fff"
  $exeAbs = (Resolve-Path $exePath).Path
  $exeDir = Split-Path -Parent $exeAbs
  $workAbs = (Resolve-Path $workDir).Path

  $stdin = Join-Path $teamDir ("{0}_{1}_{2}.stdin.txt" -f $teamKey, $tag, $t)
  # Default interactive inputs (official LUTest):
  #   1) algorithm: 0/1
  #   2) plot: 0
  (($algChoice.ToString()) + "`r`n" + (("0`r`n") * 12)) | Set-Content -NoNewline -Encoding ASCII $stdin

  $log = Join-Path $teamDir ("{0}_{1}_{2}.log.txt" -f $teamKey, $tag, $t)
  $cmdFile = Join-Path $teamDir ("{0}_{1}_{2}.run.cmd" -f $teamKey, $tag, $t)

  $start = Get-Date
  @(
    "@echo off"
    "setlocal"
    "set ""PATH=$exeDir;%PATH%"""
    "pushd ""$workAbs"""
    """$exeAbs"" < ""$stdin"" > ""$log"" 2>&1"
    "set ERR=%ERRORLEVEL%"
    "popd"
    "exit /b %ERR%"
  ) | Set-Content -Encoding Default $cmdFile

  $null = cmd.exe /d /c """$cmdFile"""

  $perfCsv = Get-ChildItem -Path $workAbs -Filter "performance_report_*.csv" -File -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $start.AddSeconds(-1) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

  $csvOut = $null
  if ($perfCsv) {
    $csvOut = Join-Path $teamDir ("{0}_{1}_{2}" -f $teamKey, $tag, $perfCsv.Name)
    Copy-Item -Force $perfCsv.FullName $csvOut
  }

  # Treat "header only" (or otherwise incomplete) performance_report as invalid so we can retry with alt input.
  if ($csvOut) {
    try {
      $rows = Import-Csv -Path $csvOut
      if (-not $rows -or $rows.Count -lt 7) { $csvOut = $null }
    } catch {
      $csvOut = $null
    }
  }

  # Some forks prompt "Select test case [1-7], 9=all" as the 2nd input and may not generate performance_report_*.csv.
  # If we didn't detect a performance report, try rerunning once with the alternative input: 9 (run all).
  if (-not $csvOut -and (Test-Path $log)) {
    $needsAlt = (Select-String -Path $log -Pattern "Select test case" -SimpleMatch -Quiet)
    if ($needsAlt) {
      $stdin2 = Join-Path $teamDir ("{0}_{1}_{2}.stdin.alt.txt" -f $teamKey, $tag, $t)
      (($algChoice.ToString()) + "`r`n9`r`n" + (("0`r`n") * 12)) | Set-Content -NoNewline -Encoding ASCII $stdin2
      $log2 = Join-Path $teamDir ("{0}_{1}_{2}.log.alt.txt" -f $teamKey, $tag, $t)
      $cmdFile2 = Join-Path $teamDir ("{0}_{1}_{2}.run.alt.cmd" -f $teamKey, $tag, $t)
      $start2 = Get-Date
      @(
        "@echo off"
        "setlocal"
        "set ""PATH=$exeDir;%PATH%"""
        "pushd ""$workAbs"""
        """$exeAbs"" < ""$stdin2"" > ""$log2"" 2>&1"
        "set ERR=%ERRORLEVEL%"
        "popd"
        "exit /b %ERR%"
      ) | Set-Content -Encoding Default $cmdFile2
      $null = cmd.exe /d /c """$cmdFile2"""

      $perfCsv2 = Get-ChildItem -Path $workAbs -Filter "performance_report_*.csv" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -gt $start2.AddSeconds(-1) } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
      if ($perfCsv2) {
        $csvOut = Join-Path $teamDir ("{0}_{1}_{2}" -f $teamKey, $tag, $perfCsv2.Name)
        Copy-Item -Force $perfCsv2.FullName $csvOut
      }
      $log = $log2
    }
  }

  return [pscustomobject]@{
    team = $team
    teamKey = $teamKey
    tag = $tag
    workDir = $workAbs
    exe = $exeAbs
    log = $log
    csv = $csvOut
  }
}

$repoRoot = Resolve-RepoRoot

# Default RefRoot is "参考项目" at repo root.
$defaultRefRoot = ([string]([char]0x53C2) + [string]([char]0x8003) + [string]([char]0x9879) + [string]([char]0x76EE))
if ([string]::IsNullOrWhiteSpace($RefRoot)) { $RefRoot = $defaultRefRoot }

$refRootAbs = (Resolve-Path (Join-Path $repoRoot $RefRoot)).Path
$casesAbs = (Resolve-Path (Join-Path $repoRoot $CasesPath)).Path

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repoRoot ("build\\ref_exe_compare_{0}" -f $ts)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("RefRoot: {0}" -f $refRootAbs)
Write-Host ("Cases:   {0}" -f $casesAbs)
Write-Host ("OutDir:  {0}" -f $outDir)
Write-Host ("Config:  {0}  Platform: {1}  Reps: {2}" -f $Config, $Platform, $Reps)

$cfgRx = ('\\' + $Platform + '\\' + $Config + '\\')
$exePaths = Get-ChildItem -Path $refRootAbs -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -ieq "SparseMatrixLUTest.exe" } |
  Where-Object { $_.FullName -match $cfgRx } |
  Select-Object -ExpandProperty FullName

if (-not $exePaths -or $exePaths.Count -eq 0) {
  throw "No SparseMatrixLUTest.exe found under RefRoot for the requested Platform/Config."
}

$rows = foreach ($exe in $exePaths) {
  $team = Get-TeamName $refRootAbs $exe
  if (-not [string]::IsNullOrWhiteSpace($TeamRegex) -and ($team -notmatch $TeamRegex)) { continue }
  $teamDir = Join-Path $refRootAbs $team
  $root = Find-ProjectRoot $exe $teamDir
  $hasSln = Test-Path (Join-Path $root "SparseMatrixLUTest.sln")
  $rank = if ($hasSln) { 1 } else { 2 }
  # Prefer runnable packaged trees that include required runtime DLLs next to the exe.
  $exeDir = Split-Path -Parent $exe
  $mustHave = @("SparseMatrixLUTestTool.dll", "klu.dll")
  foreach ($dll in $mustHave) {
    if (-not (Test-Path (Join-Path $exeDir $dll))) { $rank += 10; break }
  }
  [pscustomobject]@{ team = $team; exe = $exe; root = $root; rank = $rank }
}

$teams = $rows | Group-Object team
$summary = @()

foreach ($g in $teams) {
  $team = $g.Name
  $pick = $g.Group | Sort-Object rank | Select-Object -First 1
  $exe = $pick.exe
  $root = $pick.root

  $workDir = if (Test-Path (Join-Path $root "SparseMatrixLUTest")) { Join-Path $root "SparseMatrixLUTest" } else { $root }

  # Many LUTest builds expect cases at:
  #   - <workDir>/../PreliminaryTestCases  (official)
  #   - <exeDir>/../PreliminaryTestCases   (some packaged builds)
  # Try to ensure multiple candidate locations exist.
  $exeDirPicked = Split-Path -Parent $exe
  $caseLinks = @(
    (Join-Path $root "PreliminaryTestCases"),
    (Join-Path (Split-Path -Parent $workDir) "PreliminaryTestCases"),
    (Join-Path (Split-Path -Parent $exeDirPicked) "PreliminaryTestCases")
  ) | Sort-Object -Unique
  foreach ($cl in $caseLinks) { if (-not (Test-Path $cl)) { Ensure-Junction $cl $casesAbs } }
  $caseLink = $caseLinks[0]

  Write-Host ("== {0} ==" -f $team)
  Write-Host ("exe:      {0}" -f $exe)
  Write-Host ("root:     {0}" -f $root)
  Write-Host ("workDir:  {0}" -f $workDir)
  Write-Host ("cases:    {0}" -f $caseLink)

  $wb = @()
  $wc = @()
  $epsAll = @()
  $errAll = @()
  $baselineRuns = @()
  $candidateRuns = @()

  for ($i = 1; $i -le $Reps; $i++) {
    # ABAB: alternate baseline and candidate to reduce drift from thermals/background load.
    Write-Host ("  baseline rep {0}/{1}..." -f $i, $Reps)
    $rb = Invoke-LUTest $team ("baseline_r{0}" -f $i) $workDir $exe 0 $outDir
    $baselineRuns += $rb
    if ($rb.csv) { $wb += (Weighted-Total $rb.csv) } else { $wb += (Weighted-FromLog $rb.log) }
    $epsAll += ,(Parse-XRelErrors $rb.log)

    Write-Host ("  candidate rep {0}/{1}..." -f $i, $Reps)
    $rc = Invoke-LUTest $team ("candidate_r{0}" -f $i) $workDir $exe 1 $outDir
    $candidateRuns += $rc
    if ($rc.csv) { $wc += (Weighted-Total $rc.csv) } else { $wc += (Weighted-FromLog $rc.log) }
    $errAll += ,(Parse-XRelErrors $rc.log)
  }

  # Use first run's errors as representative (they are typically deterministic for the given toolchain).
  $eps = $null
  foreach ($e in $epsAll) { if ($e.Count -ge 7) { $eps = $e; break } }
  $err = $null
  foreach ($e in $errAll) { if ($e.Count -ge 7) { $err = $e; break } }

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

  $summary += [pscustomobject]@{
    team = $team
    team_key = (Team-Key $team)
    exe = $exe
    root = $root
    workDir = $workDir
    reps = $Reps
    pass_7of7_err_le_baseline = $pass
    pass_count = $passCount
    min_margin_eps_minus_user = $minMargin
    weighted_baseline_median_ms = $wbMed
    weighted_candidate_median_ms = $wcMed
    speedup_baseline_over_candidate = $speedup
    baseline_logs = ($baselineRuns | Select-Object -ExpandProperty log) -join ";"
    candidate_logs = ($candidateRuns | Select-Object -ExpandProperty log) -join ";"
    baseline_csvs = ($baselineRuns | Select-Object -ExpandProperty csv) -join ";"
    candidate_csvs = ($candidateRuns | Select-Object -ExpandProperty csv) -join ";"
  }
}

$summaryPath = Join-Path $outDir "summary.csv"
$summary |
  Sort-Object weighted_candidate_median_ms, team |
  Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath

Write-Host ("Summary: {0}" -f $summaryPath)
