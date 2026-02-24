Param(
  [Parameter(Mandatory = $false)]
  [string]$Root
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$versionFile = Join-Path $Root "VERSION"
if (-not (Test-Path $versionFile)) {
  throw "VERSION file not found at '$versionFile'."
}

$version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($version)) {
  throw "VERSION file is empty."
}

function Update-TextFileReplace {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Pattern,
    [Parameter(Mandatory = $true)][string]$Replacement
  )

  if (-not (Test-Path $Path)) {
    throw "File not found: $Path"
  }

  $content = Get-Content -LiteralPath $Path -Raw
  $newContent = [System.Text.RegularExpressions.Regex]::Replace(
    $content,
    $Pattern,
    $Replacement,
    [System.Text.RegularExpressions.RegexOptions]::Multiline
  )

  if ($newContent -ne $content) {
    Set-Content -LiteralPath $Path -Value $newContent -Encoding UTF8
  }
}

# 1) C# loader version constant
$programCs = Join-Path $Root "loader\Program.cs"
Update-TextFileReplace `
  -Path $programCs `
  -Pattern 'public const string VERSION\s*=\s*"[^"]+";' `
  -Replacement ('public const string VERSION = "' + $version + '";')

# 2) plugins/package.json version
$packageJson = Join-Path $Root "plugins\package.json"
Update-TextFileReplace `
  -Path $packageJson `
  -Pattern '("version"\s*:\s*")[^"]+(")' `
  -Replacement ('$1' + $version + '$2')

# 3) plugins/package-lock.json top-level + packages[""] version
$packageLock = Join-Path $Root "plugins\package-lock.json"
if (Test-Path $packageLock) {
  $lockContent = Get-Content -LiteralPath $packageLock -Raw

  # top-level "version"
  $lockContent2 = [System.Text.RegularExpressions.Regex]::Replace(
    $lockContent,
    '(^\s*"version"\s*:\s*")[^"]+(")',
    ('$1' + $version + '$2'),
    [System.Text.RegularExpressions.RegexOptions]::Multiline
  )

  # packages[""] "version" (the root package entry)
  $lockContent3 = [System.Text.RegularExpressions.Regex]::Replace(
    $lockContent2,
    '("packages"\s*:\s*\{\s*\r?\n\s*""\s*:\s*\{\s*\r?\n(?:.*\r?\n)*?\s*"version"\s*:\s*")[^"]+(")',
    ('$1' + $version + '$2'),
    [System.Text.RegularExpressions.RegexOptions]::Multiline
  )

  if ($lockContent3 -ne $lockContent) {
    Set-Content -LiteralPath $packageLock -Value $lockContent3 -Encoding UTF8
  }
}

Write-Host "Synced version to $version"
