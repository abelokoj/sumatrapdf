param([ValidateSet('x64','arm64')][string]$Architecture = 'x64', [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../enhanced-version.txt') -Raw).Trim())
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path $PSScriptRoot -Parent
$binaryDir = Join-Path $repoDir $(if ($Architecture -eq 'arm64') { 'out/arm64' } else { 'out/rel64' })
$stageDir = Join-Path $repoDir "dist/stage-$Architecture/SumatraPDF-Enhanced-$Version"
$expectedStage = [IO.Path]::GetFullPath((Join-Path $repoDir "dist/stage-$Architecture"))
$resolvedStage = [IO.Path]::GetFullPath($stageDir)
if (-not $resolvedStage.StartsWith($expectedStage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid packaging stage' }
if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
$machine = if ($Architecture -eq 'arm64') { 0xAA64 } else { 0x8664 }
foreach ($name in @('SumatraPDF.exe','libsumatrapdf.dll','PdfFilter.dll','PdfPreview.dll','sumatrapdf-tool.exe','SumatraPDF-static.exe')) {
    $source = Join-Path $binaryDir $name
    $bytes = [IO.File]::ReadAllBytes($source)
    $offset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([BitConverter]::ToUInt16($bytes, $offset + 4) -ne $machine) { throw "Wrong binary architecture: $name" }
    if ($name -ne 'SumatraPDF-static.exe') { Copy-Item -LiteralPath $source -Destination $stageDir }
}
foreach ($name in @('AUTHORS','COPYING','COPYING.BSD')) {
    Copy-Item -LiteralPath (Join-Path $repoDir $name) -Destination $stageDir
}
Copy-Item -LiteralPath (Join-Path $repoDir 'src/dictionaries') -Destination (Join-Path $stageDir 'dictionaries') -Recurse
$docsDir = Join-Path $stageDir 'docs'
New-Item -ItemType Directory -Force -Path $docsDir | Out-Null
foreach ($name in @('licenses','dictionaries','font-attribution.md','icon-attribution.md','vocabulary-attribution.md')) {
    Copy-Item -LiteralPath (Join-Path $repoDir "docs/$name") -Destination (Join-Path $docsDir $name) -Recurse
}
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir 'data') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoDir 'data/vocabulary') -Destination (Join-Path $stageDir 'data/vocabulary') -Recurse
$sourceSha = git -C $repoDir rev-parse HEAD
@"
SumatraPDF Enhanced $Version - Windows $Architecture
Extract the entire folder and run SumatraPDF.exe. Keep dictionaries beside it.
Select a word and press Shift+D for offline dictionary lookup.
The home learning hub opens vocabulary and practice activities.
Release information: https://github.com/abelokoj/sumatrapdf-enhanced/releases/latest
Source commit: $sourceSha
Original SumatraPDF: 3.7 source snapshot, a0d8bcaed0412ce803d9c213845e710fcbe3c7a5.
"@ | Set-Content -LiteralPath (Join-Path $stageDir 'README.txt') -Encoding utf8
@"
Original SumatraPDF version: 3.7 source snapshot
Upstream commit: a0d8bcaed0412ce803d9c213845e710fcbe3c7a5
Upstream source snapshot date: 2026-09-30 09:55:54 UTC
Enhanced source commit: $sourceSha
Architecture: $Architecture
Build: GitHub-hosted Windows runner
"@ | Set-Content -LiteralPath (Join-Path $stageDir 'UPSTREAM_BASE.txt') -Encoding utf8
@"
Theme = Sumatra Light
CheckForUpdates = false
RememberOpenedFiles = true
UseTabs = true
ReuseInstance = false
UIFontFamily = system
"@ | Set-Content -LiteralPath (Join-Path $stageDir 'SumatraPDF-settings.txt') -Encoding utf8
$archive = Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-portable.zip"
Compress-Archive -Path $stageDir -DestinationPath $archive -Force
$installer = Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-install.exe"
Copy-Item -LiteralPath (Join-Path $binaryDir 'SumatraPDF.exe') -Destination $installer
$portable = Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-portable.exe"
Copy-Item -LiteralPath (Join-Path $binaryDir 'SumatraPDF-static.exe') -Destination $portable
Write-Output $portable
Write-Output $archive
Write-Output $installer
