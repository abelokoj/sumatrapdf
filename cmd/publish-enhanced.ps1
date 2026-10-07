param(
    [Parameter(Mandatory)][ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$SourceCommit
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$files = @()
foreach ($arch in @('x64','arm64')) {
    $expectedMachine = if ($arch -eq 'x64') { 0x8664 } else { 0xAA64 }
    foreach ($kind in @('install.exe','portable.exe','portable.zip')) {
        $name = "SumatraPDF-Enhanced-$Version-$arch-$kind"
        $path = Join-Path $PackageDirectory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release package: $name" }
        if ($kind.EndsWith('.exe')) {
            $bytes = [IO.File]::ReadAllBytes($path)
            $offset = [BitConverter]::ToInt32($bytes, 0x3C)
            if ([BitConverter]::ToUInt16($bytes, $offset + 4) -ne $expectedMachine) { throw "Incorrect architecture: $name" }
        } else {
            $zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $path))
            try {
                foreach ($entry in $zip.Entries) {
                    if ($entry.Name -in @('PrettySumatraPDF_TODO.md','RELEASE_NOTES.md','CHANGELOG.md')) { throw 'Private record in release ZIP' }
                }
            } finally { $zip.Dispose() }
        }
        $files += $path
    }
}
if (@(Get-ChildItem -LiteralPath $PackageDirectory -File).Count -ne 6) { throw 'Expected exactly four executables and two ZIPs' }
$upstream = Get-Content -LiteralPath (Join-Path $repoRoot 'enhanced-upstream.json') -Raw | ConvertFrom-Json
if ($upstream.commit -notmatch '^[0-9a-f]{40}$') { throw 'Invalid upstream source metadata' }
$changelog = Get-Content -LiteralPath (Join-Path $repoRoot 'CHANGELOG.md') -Raw
$entry = [regex]::Match($changelog, '(?ms)^## ' + [regex]::Escape($Version) + '\s*\n(.*?)(?=^## |\z)').Groups[1].Value.Trim()
if (-not $entry) { throw 'Missing public changelog entry for this release' }
$notes = @"
SumatraPDF Enhanced $Version

$entry

## Downloads

- x64 installer EXE and standalone portable EXE: for most Intel and AMD computers.
- ARM64 installer EXE and standalone portable EXE: for ARM devices.
- Portable ZIPs for both architectures include the reader, dictionaries, license notices and optional shell helpers.

Run a portable EXE directly, with no installation or ZIP extraction. The offline dictionary is embedded in the executable. Run an installer EXE to install the application. Extract the entire folder when using a ZIP package.

The app includes a native themed interface, pen profiles and favorite annotation presets, a temporary laser pointer, offline dictionary lookup with Shift+D, vocabulary lists and practice activities. The green logo appears throughout the application. Optional browser integrations require the separate WebView2 runtime. Reference previews require supported local destinations. Advanced handwriting recognition is not available, and laser behavior and pen responsiveness are still being refined.

## Upstream source

Based on the **SumatraPDF $($upstream.version)** at upstream commit [$($upstream.commit)](https://github.com/sumatrapdfreader/sumatrapdf/commit/$($upstream.commit)), dated **$($upstream.commitDateUtc)**. This is the upstream commit date.

Enhanced source: [$SourceCommit](https://github.com/abelokoj/sumatrapdf-enhanced/commit/$SourceCommit).

Bundled fonts, icons and dictionary data retain their separate licenses. Notices are included in the application payload and in the source repository.
"@
$notesPath = Join-Path $env:RUNNER_TEMP 'enhanced-public-release-notes.md'
[IO.File]::WriteAllText($notesPath, $notes)
$tag = "enhanced-$Version"
# GitHub creates the tag at the checked source commit only after both builds pass.
$existingJson = & gh release view $tag --json isDraft,targetCommitish 2>$null
if ($LASTEXITCODE -eq 0) {
    $existing = $existingJson | ConvertFrom-Json
    if (-not $existing.isDraft) { throw 'Published versions are preserved; choose a new version' }
    if ($existing.targetCommitish -ne $SourceCommit) { throw 'Existing draft belongs to a different source commit' }
    & gh release upload $tag @files --clobber
} else {
    & gh release create $tag --target $SourceCommit --title "SumatraPDF Enhanced $Version" --notes-file $notesPath --draft @files
}
if ($LASTEXITCODE -ne 0) { throw 'Release upload failed; any draft remains unpublished' }
$releaseJson = & gh api "repos/$env:GH_REPO/releases?per_page=100"
if ($LASTEXITCODE -ne 0) { throw 'Unable to verify uploaded release files' }
$release = @($releaseJson | ConvertFrom-Json | Where-Object { $_.tag_name -eq $tag })
if ($release.Count -ne 1 -or -not $release[0].draft) { throw 'Expected one unpublished draft for this version' }
$release = $release[0]
if ($release.assets.Count -ne 6) { throw 'Expected exactly six uploaded app packages' }
foreach ($file in $files) {
    $name = Split-Path $file -Leaf
    $asset = @($release.assets | Where-Object { $_.name -eq $name })
    $digest = 'sha256:' + (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($asset.Count -ne 1 -or $asset[0].digest -ne $digest) { throw "Upload checksum mismatch: $name" }
}
& gh release edit $tag --draft=false --latest
if ($LASTEXITCODE -ne 0) { throw 'Unable to publish completed draft release' }
Write-Output "Published $tag with four executables and two portable ZIPs."
