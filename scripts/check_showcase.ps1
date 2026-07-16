$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$showcaseRoot = Join-Path $repoRoot "showcase"
$sources = @(
  (Join-Path $showcaseRoot "index.html"),
  (Join-Path $showcaseRoot "styles.css"),
  (Join-Path $showcaseRoot "app.js")
)

$references = New-Object System.Collections.Generic.List[string]
foreach ($source in $sources) {
  $content = Get-Content -Raw -Path $source
  foreach ($pattern in @(
    '(?:src|href)="([^"]+)"',
    '(?:src|href):\s*"([^"]+)"',
    'fetch\("([^"]+)"',
    'url\(["'']?([^\)"'']+)'
  )) {
    foreach ($match in [regex]::Matches($content, $pattern)) {
      $references.Add($match.Groups[1].Value)
    }
  }
}

$missing = New-Object System.Collections.Generic.List[string]
foreach ($reference in ($references | Sort-Object -Unique)) {
  if ($reference.Contains('${') -or
      $reference.StartsWith("#") -or
      $reference.StartsWith("http://") -or
      $reference.StartsWith("https://") -or
      $reference.StartsWith("mailto:")) {
    continue
  }
  $pathPart = $reference.Split("?")[0]
  $resolved = [System.IO.Path]::GetFullPath((Join-Path $showcaseRoot $pathPart))
  if (!(Test-Path $resolved)) { $missing.Add($reference) }
}

if ($missing.Count -gt 0) {
  throw "Showcase contains missing references: $($missing -join ', ')"
}

Write-Host "Showcase references verified: $($references.Count) references checked."
