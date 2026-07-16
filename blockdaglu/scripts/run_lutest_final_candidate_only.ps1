param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("Release", "Debug")]
  [string]$Config = "Release",

  # Forwarded into candidate env. Use 0 to let algorithm self-select.
  [Parameter(Mandatory = $false)]
  [int]$Threads = 0,

  [Parameter(Mandatory = $false)]
  [ValidateSet("default", "auto", "klu", "block")]
  [string]$Mode = "default",

  [Parameter(Mandatory = $false)]
  [string]$FinalRoot = "",

  [Parameter(Mandatory = $true)]
  [string]$CandidateDll,

  [Parameter(Mandatory = $false)]
  [string[]]$ExtraRuntimeDlls = @(),

  [Parameter(Mandatory = $false)]
  [string]$Tag = "candidate_only"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot { (Resolve-Path (Join-Path $PSScriptRoot "..\\..")).Path }

function Case-Weight([int]$n) {
  if ($n -le 20000) { return 0.1 }
  if ($n -le 40000) { return 0.15 }
  if ($n -le 55000) { return 0.25 }
  return 0.5
}

function Parse-CaseSizes([string]$logPath) {
  $text = Get-Content -Path $logPath -Raw -Encoding Default
  $map = @{}
  $tableRx = [regex]'(?m)^TestCase_(\d+)\s+.*?\s+(\d+)\*(\d+)\s+'
  foreach ($m in $tableRx.Matches($text)) {
    $id = [int]$m.Groups[1].Value
    $n = [int]$m.Groups[2].Value
    $map[$id] = $n
  }
  return $map
}

function Read-PerfCsv([string]$csvPath) {
  $rows = Import-Csv -Path $csvPath
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
  foreach ($id in $perfByCase.Keys) {
    $n = [int]$caseSizes[$id]
    $w = Case-Weight $n
    $s += $w * [double]$perfByCase[$id].total_ms
  }
  return $s
}

function Run-Candidate([string]$exe, [string]$dllDir, [string]$workDir, [string]$outDir, [string]$tag) {
  $t = Get-Date -Format "yyyyMMdd_HHmmss_fff"
  $stdin = Join-Path $outDir ("{0}_{1}.stdin.txt" -f $tag, $t)
  ("1`r`n0`r`n") | Set-Content -NoNewline -Encoding ASCII $stdin

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

  if (-not $perfCsv) { throw "Missing performance_report_*.csv after run." }
  $csvOut = Join-Path $outDir ("{0}_{1}" -f $tag, $perfCsv.Name)
  Copy-Item -Force $perfCsv.FullName $csvOut

  return [pscustomobject]@{ log = $log; csv = $csvOut }
}

$repo = Resolve-RepoRoot
if (-not $FinalRoot) { $FinalRoot = Join-Path $repo "SparseMatrixLUTest_Final" }
$FinalRoot = (Resolve-Path $FinalRoot).Path

$exe = Join-Path $FinalRoot ("x64\\{0}\\SparseMatrixLUTest.exe" -f $Config)
$dllDir = Join-Path $FinalRoot ("x64\\{0}" -f $Config)
$workDir = Join-Path $FinalRoot "SparseMatrixLUTest"
if (-not (Test-Path $exe)) { throw "Missing exe: $exe" }
if (-not (Test-Path $dllDir)) { throw "Missing dll dir: $dllDir" }
if (-not (Test-Path $workDir)) { throw "Missing work dir: $workDir" }
if (-not (Test-Path $CandidateDll)) { throw "Missing CandidateDll: $CandidateDll" }

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repo ("build\\lutest_final_candidate_only_{0}_{1}" -f $ts, $Tag)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host ("OutDir:  {0}" -f $outDir)
Write-Host ("Final:   {0}" -f $FinalRoot)
Write-Host ("Config:  {0}  Mode: {1}  Threads: {2}" -f $Config, $Mode, $Threads)

# Place candidate DLL next to SparseMatrixLUTest.exe
$dstDll = Join-Path $dllDir "useralgo_baseline.dll"
Copy-Item -Force $CandidateDll $dstDll
foreach ($d in $ExtraRuntimeDlls) {
  if ($d -and (Test-Path $d)) {
    Copy-Item -Force $d (Join-Path $dllDir (Split-Path $d -Leaf))
  }
}

# Candidate env
Remove-Item Env:USERALGO_MODE -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_THREADS -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_PROFILE -ErrorAction SilentlyContinue
Remove-Item Env:USERALGO_REFINE_ITERS -ErrorAction SilentlyContinue
$env:USERALGO_PROFILE = "0"
$env:USERALGO_REFINE_ITERS = "0"
if ($Mode -ne "default") { $env:USERALGO_MODE = $Mode }
if ($Threads -gt 0) { $env:USERALGO_THREADS = "$Threads" }

$run = Run-Candidate $exe $dllDir $workDir $outDir "candidate"
$caseSizes = Parse-CaseSizes $run.log
if (-not $caseSizes -or $caseSizes.Count -lt 15) {
  throw "Failed to parse case sizes from log (parsed $($caseSizes.Count))."
}
$perf = Read-PerfCsv $run.csv
$weighted = Weighted-Total $caseSizes $perf

$summary = [pscustomobject]@{
  mode = $Mode
  threads = $Threads
  weighted_candidate_ms = $weighted
  perf_csv = $run.csv
  log = $run.log
}
$summaryPath = Join-Path $outDir "summary.csv"
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath

Write-Host ("Summary: {0}" -f $summaryPath)

[pscustomobject]@{
  out_dir = $outDir
  summary_csv = $summaryPath
  weighted_candidate_ms = $weighted
}

