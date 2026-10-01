param([ValidateSet('x64','arm64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path $PSScriptRoot -Parent
$binaryDir = Join-Path $repoDir $(if ($Architecture -eq 'arm64') { 'out/arm64' } else { 'out/rel64' })
$stageDir = Join-Path $repoDir "dist/stage-$Architecture/SumatraPDF-Enhanced-preview-03"
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
$machine = if ($Architecture -eq 'arm64') { 0xAA64 } else { 0x8664 }
foreach ($name in @('SumatraPDF.exe','libsumatrapdf.dll','PdfFilter.dll','PdfPreview.dll','sumatrapdf-tool.exe')) {
    $source = Join-Path $binaryDir $name
    $bytes = [IO.File]::ReadAllBytes($source)
    $offset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([BitConverter]::ToUInt16($bytes, $offset + 4) -ne $machine) { throw "Wrong binary architecture: $name" }
    Copy-Item -LiteralPath $source -Destination $stageDir
}
foreach ($name in @('AUTHORS','COPYING','COPYING.BSD','CHANGELOG.md','PrettySumatraPDF_TODO.md','RELEASE_NOTES.md')) {
    Copy-Item -LiteralPath (Join-Path $repoDir $name) -Destination $stageDir
}
Copy-Item -LiteralPath (Join-Path $repoDir 'src/dictionaries') -Destination (Join-Path $stageDir 'dictionaries') -Recurse
Copy-Item -LiteralPath (Join-Path $repoDir 'docs') -Destination (Join-Path $stageDir 'docs') -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir 'data') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoDir 'data/vocabulary') -Destination (Join-Path $stageDir 'data/vocabulary') -Recurse
$sourceSha = git -C $repoDir rev-parse HEAD
@"
SumatraPDF Enhanced Preview 03 - Windows $Architecture
Extract the entire folder and run SumatraPDF.exe. Keep dictionaries beside it.
Select a word and press Shift+D for offline dictionary lookup.
The home learning hub opens vocabulary and practice activities.
See RELEASE_NOTES.md for changes, validation and known limitations.
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
$archive = Join-Path $repoDir "dist/SumatraPDF-Enhanced-preview-03-windows-$Architecture.zip"
Compress-Archive -Path $stageDir -DestinationPath $archive -Force
Write-Output $archive
