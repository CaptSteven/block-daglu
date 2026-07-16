param(
  [Parameter(Mandatory = $false)]
  [string]$CasesPath = "SparseMatrixLUTest\\PreliminaryTestCases",

  [Parameter(Mandatory = $false)]
  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",

  [Parameter(Mandatory = $false)]
  [int]$Threads = 0,

  [Parameter(Mandatory = $false)]
  [int]$CaseId = 7,

  [Parameter(Mandatory = $false)]
  [string]$Orderings = "1,0",

  [Parameter(Mandatory = $false)]
  [string]$Scales = "0,2,1",

  [Parameter(Mandatory = $false)]
  [string]$Btf = "0,1"
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
$env:USERALGO_MODE = "block"
$env:USERALGO_PROFILE = "0"
$env:USERALGO_REFINE_ITERS = "0"

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $root ("build\\sweep_block_klu_{0}" -f $ts)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$csv = Join-Path $outDir "sweep.csv"
[System.IO.File]::WriteAllText(
  $csv,
  "ordering,scale,btf,reorder_ms,lu_ms,solve_ms,total_ms,err_user,eps_suitesparse,margin,ok,log`r`n",
  [System.Text.UTF8Encoding]::new($false)
)

function Parse-AlgRow([string]$text) {
  # Example:
  # UserAlgorithm,7,61531,3047885,849.939,1607.920,38.239,2496.098,5.93e-02,1.93e-18,1
  $lines = [regex]::Split($text, '\r?\n')
  $line = ($lines | Where-Object { $_ -like 'UserAlgorithm,*' } | Select-Object -First 1)
  if (-not $line) { return $null }
  $p = $line.Split(',')
  if ($p.Count -lt 11) { return $null }
  return @{
    reorder = [double]$p[4]
    lu = [double]$p[5]
    solve = [double]$p[6]
    total = [double]$p[7]
    err = [double]$p[8]
    ok = [int]$p[10]
  }
}

function Parse-MarginRow([string]$text) {
  # Example:
  # 7,61531,3047885,...,eps_suitesparse,err_user,margin_eps_minus_user,pass_user_le_eps
  $lines = [regex]::Split($text, '\r?\n')
  $line = ($lines | Where-Object { $_ -match ('^{0},\d' -f $CaseId) } | Select-Object -First 1)
  if (-not $line) { return $null }
  $p = $line.Split(',')
  if ($p.Count -lt 10) { return $null }
  return @{
    eps = [double]$p[6]
    err = [double]$p[7]
    margin = [double]$p[8]
    pass = [int]$p[9]
  }
}

$orderingsList = @()
$scalesList = @()
$btfList = @()

foreach ($x in ($Orderings -split ',')) {
  $t = $x.Trim()
  if ($t -eq '') { continue }
  $v = [int]$t
  if ($v -ge 0 -and $v -le 3) { $orderingsList += $v }
}
foreach ($x in ($Scales -split ',')) {
  $t = $x.Trim()
  if ($t -eq '') { continue }
  $v = [int]$t
  if ($v -ge -1 -and $v -le 2) { $scalesList += $v }
}
foreach ($x in ($Btf -split ',')) {
  $t = $x.Trim()
  if ($t -eq '') { continue }
  $v = [int]$t
  if ($v -eq 0 -or $v -eq 1) { $btfList += $v }
}

if ($orderingsList.Count -eq 0) { $orderingsList = @(1) }
if ($scalesList.Count -eq 0) { $scalesList = @(0) }
if ($btfList.Count -eq 0) { $btfList = @(0) }

foreach ($ordering in $orderingsList) {
  foreach ($scale in $scalesList) {
    foreach ($btf in $btfList) {
      $env:USERALGO_BLOCK_KLU_ORDERING = "$ordering"
      $env:USERALGO_BLOCK_KLU_SCALE = "$scale"
      $env:USERALGO_BLOCK_KLU_BTF = "$btf"

      $log = Join-Path $outDir ("case{0}_ordering{1}_scale{2}_btf{3}.txt" -f $CaseId,$ordering,$scale,$btf)
      $outText = (& $exe $cases $CaseId 2>&1 | Out-String)
      [System.IO.File]::WriteAllText($log, $outText, [System.Text.Encoding]::Unicode)
      $text = [System.IO.File]::ReadAllText($log, [System.Text.Encoding]::Unicode)

      $alg = Parse-AlgRow $text
      $mrg = Parse-MarginRow $text
      if (-not $alg) { continue }

      $err_user = if ($mrg) { $mrg.err } else { $alg.err }
      $eps = if ($mrg) { $mrg.eps } else { [double]::NaN }
      $margin = if ($mrg) { $mrg.margin } else { [double]::NaN }
      $ok = if ($mrg) { $mrg.pass } else { $alg.ok }

      $row = "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11}" -f `
        $ordering,$scale,$btf, `
        $alg.reorder,$alg.lu,$alg.solve,$alg.total, `
        $err_user,$eps,$margin,$ok, `
        (Split-Path -Leaf $log)
      [System.IO.File]::AppendAllText($csv, ($row + "`r`n"), [System.Text.UTF8Encoding]::new($false))
    }
  }
}

Write-Host ("Done. CSV: {0}" -f $csv)
Write-Host ("Logs: {0}" -f $outDir)
