param(
  [Parameter(Mandatory = $true)]
  [string]$BenchmarkDir,
  [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BenchmarkDir = (Resolve-Path $BenchmarkDir).Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $OutputPath = Join-Path $repoRoot "showcase\data\results.json"
}

$timing = Import-Csv (Join-Path $BenchmarkDir "timing_summary.csv")
$margin = Import-Csv (Join-Path $BenchmarkDir "margin.csv")
$metadataPath = Join-Path $BenchmarkDir "metadata.json"
$metadata = if (Test-Path $metadataPath) { Get-Content -Raw $metadataPath | ConvertFrom-Json } else { $null }

function Weight-ForN([int]$n) {
  if ($n -le 20000) { return 0.10 }
  if ($n -le 40000) { return 0.15 }
  if ($n -le 55000) { return 0.25 }
  return 0.50
}

$cases = @()
for ($caseId = 1; $caseId -le 15; $caseId++) {
  $baseline = $timing | Where-Object { $_.alg -eq "SuiteSparse" -and [int]$_.case -eq $caseId } | Select-Object -First 1
  $ours = $timing | Where-Object { $_.alg -eq "BlockDAGLU" -and [int]$_.case -eq $caseId } | Select-Object -First 1
  $gate = $margin | Where-Object { [int]$_.case -eq $caseId } | Select-Object -First 1
  if ($null -eq $baseline -or $null -eq $ours -or $null -eq $gate) {
    throw "Benchmark output is missing case $caseId."
  }
  if ($gate.pass_user_le_eps -ne "1" -or $ours.ok -ne "1") {
    throw "Case $caseId did not pass correctness and precision gates."
  }

  $baselineMs = [double]$baseline.median_total_ms
  $oursMs = [double]$ours.median_total_ms
  $strategy = if ([string]::IsNullOrWhiteSpace($ours.strategy)) { "UNKNOWN" } else { $ours.strategy }
  $cases += [ordered]@{
    id = $caseId
    n = [int]$ours.n
    nnz = [int64]$ours.nnz
    weight = Weight-ForN ([int]$ours.n)
    strategy = $strategy
    baseline = $baselineMs
    ours = $oursMs
    baselineP95 = [double]$baseline.p95_total_ms
    oursP95 = [double]$ours.p95_total_ms
    speedup = $baselineMs / $oursMs
    story = if ($strategy -eq "BLOCK") {
      "当前复现结果选择 BLOCK 路线，结构分解与块级并行覆盖了构建成本。"
    } else {
      "当前复现结果选择 KLU 路线，避免不必要的块分解开销。"
    }
  }
}

$weightedBaseline = ($cases | ForEach-Object { $_.weight * $_.baseline } | Measure-Object -Sum).Sum
$weightedOurs = ($cases | ForEach-Object { $_.weight * $_.ours } | Measure-Object -Sum).Sum
$payload = [ordered]@{
  status = "verified"
  verified = $true
  source = "Final15 repeated benchmark"
  generated_at_utc = [DateTime]::UtcNow.ToString("o")
  metadata = $metadata
  weighted_baseline_ms = $weightedBaseline
  weighted_ours_ms = $weightedOurs
  speedup = $weightedBaseline / $weightedOurs
  cases = $cases
}

New-Item -ItemType Directory -Force -Path (Split-Path $OutputPath -Parent) | Out-Null
$payload | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $OutputPath
Write-Host "Verified showcase results written to: $OutputPath"
