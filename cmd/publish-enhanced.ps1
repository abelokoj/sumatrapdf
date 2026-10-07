param(
    [Parameter(Mandatory)][ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$SourceCommit
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
if ($env:GH_REPO -ne 'abelokoj/sumatrapdf-enhanced') { throw 'Publication requires the canonical Enhanced repository' }
$upstream = Get-Content -LiteralPath (Join-Path $repoRoot 'enhanced-upstream.json') -Raw | ConvertFrom-Json
if ($upstream.commit -notmatch '^[0-9a-f]{40}$') { throw 'Invalid upstream source metadata' }
function Check-Pe([byte[]]$bytes,[int]$machine,[string]$name) {
    if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes,0) -ne 0x5A4D) { throw "Invalid PE binary: $name" }
    $offset = [BitConverter]::ToInt32($bytes,0x3C)
    if ($offset -lt 64 -or $offset -gt $bytes.Length - 24 -or [BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550) { throw "Invalid PE header: $name" }
    if ([BitConverter]::ToUInt16($bytes,$offset + 4) -ne $machine) { throw "Incorrect architecture: $name" }
}
function Check-Provenance($manifest,[string]$arch) {
    if ($manifest.schema -ne 1 -or $manifest.sourceCommit -ne $SourceCommit -or $manifest.sourceDirty -ne $false -or $manifest.version -ne $Version -or $manifest.architecture -ne $arch -or $manifest.upstream.commit -ne $upstream.commit) { throw 'Source provenance mismatch' }
    if ($manifest.signing -notin @('signed','unsigned','mixed')) { throw 'Missing signing status' }
}
$files = @()
$signing = @()
foreach ($arch in @('x64','arm64')) {
    $expectedMachine = if ($arch -eq 'x64') { 0x8664 } else { 0xAA64 }
    $archFiles = @(foreach ($kind in @('install.exe','portable.exe','portable.zip')) {
        $name = "SumatraPDF-Enhanced-$Version-$arch-$kind"
        $path = Join-Path $PackageDirectory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release package: $name" }
        if ($kind.EndsWith('.exe')) { Check-Pe ([IO.File]::ReadAllBytes($path)) $expectedMachine $name }
        $path
    })
    $manifestPath = Join-Path $PackageDirectory "SumatraPDF-Enhanced-$Version-$arch-manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Missing build manifest: $arch" }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Check-Provenance $manifest $arch
    $signing += "$arch $($manifest.signing)"
    if (@($manifest.packages).Count -ne 3) { throw 'Expected three packages in build manifest' }
    foreach ($path in $archFiles) {
        $file = Get-Item -LiteralPath $path
        $record = @($manifest.packages | Where-Object { $_.name -eq $file.Name })
        if ($record.Count -ne 1 -or $record[0].bytes -ne $file.Length -or $record[0].sha256 -ne (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()) { throw "Package checksum mismatch: $($file.Name)" }
    }
    $zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($archFiles[2]))
    try {
        $entries = @($zip.Entries | Where-Object { $_.Name })
        foreach ($entry in $entries) {
            $entryPath = $entry.FullName.Replace('\','/')
            if ($entryPath -match '(^|/)(private-development|\.git|\.codex|work|artifacts)(/|$)' -or $entry.Name -in @('PrettySumatraPDF_TODO.md','RELEASE_NOTES.md','CHANGELOG.md') -or $entryPath -match '(^|/)\.\.(/|$)' -or $entryPath.StartsWith('/')) { throw 'Private record or unsafe path in release ZIP' }
        }
        $embedded = @($entries | Where-Object { $_.Name -eq 'BUILD_MANIFEST.json' })
        if ($embedded.Count -ne 1) { throw 'Missing ZIP build manifest' }
        $reader = [IO.StreamReader]::new($embedded[0].Open())
        try { $payloadJson = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $payload = $payloadJson | ConvertFrom-Json
        Check-Provenance $payload $arch
        $prefix = $embedded[0].FullName.Substring(0,$embedded[0].FullName.Length - 'BUILD_MANIFEST.json'.Length)
        if (@($payload.files).Count -ne $entries.Count - 1) { throw 'ZIP payload inventory mismatch' }
        foreach ($record in $payload.files) {
            $entry = @($entries | Where-Object { $_.FullName -eq $prefix + $record.path })
            if ($entry.Count -ne 1 -or $entry[0].Length -ne $record.bytes) { throw "ZIP payload mismatch: $($record.path)" }
            $stream = $entry[0].Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $digest = [Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant() } finally { $stream.Dispose(); $sha.Dispose() }
            if ($digest -ne $record.sha256) { throw "ZIP payload checksum mismatch: $($record.path)" }
            if ($record.path -eq 'SumatraPDF.exe') {
                $stream = $entry[0].Open(); $memory = [IO.MemoryStream]::new()
                try { $stream.CopyTo($memory); Check-Pe $memory.ToArray() $expectedMachine $record.path } finally { $stream.Dispose(); $memory.Dispose() }
                $installerRecord = @($manifest.packages | Where-Object { $_.name -eq "SumatraPDF-Enhanced-$Version-$arch-install.exe" })
                if ($installerRecord.Count -ne 1 -or $digest -ne $installerRecord[0].sha256) { throw 'ZIP executable differs from installer package' }
            }
        }
        if (-not @($payload.files | Where-Object { $_.path -eq 'SumatraPDF.exe' }).Count) { throw 'Missing ZIP executable' }
        if ($manifest.payloadManifestSha256) {
            $stream = $embedded[0].Open(); $sha = [Security.Cryptography.SHA256]::Create()
            try { $digest = [Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant() } finally { $stream.Dispose(); $sha.Dispose() }
            if ($digest -ne $manifest.payloadManifestSha256) { throw 'ZIP manifest checksum mismatch' }
        }
    } finally { $zip.Dispose() }
    $sumsPath = Join-Path $PackageDirectory "SumatraPDF-Enhanced-$Version-$arch-SHA256SUMS.txt"
    if (-not (Test-Path -LiteralPath $sumsPath -PathType Leaf)) { throw 'Missing package checksums' }
    $sums = @(Get-Content -LiteralPath $sumsPath | Where-Object { $_ })
    $hashedFiles = @($archFiles) + $manifestPath
    if ($sums.Count -ne $hashedFiles.Count) { throw 'Checksum inventory mismatch' }
    foreach ($path in $hashedFiles) {
        $expected = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() + '  ' + (Split-Path $path -Leaf)
        if (@($sums | Where-Object { $_ -ceq $expected }).Count -ne 1) { throw 'Package checksum list mismatch' }
    }
    $files += $hashedFiles + $sumsPath
}
if (@(Get-ChildItem -LiteralPath $PackageDirectory -File).Count -ne $files.Count) { throw 'Unexpected release files' }
$changelog = Get-Content -LiteralPath (Join-Path $repoRoot 'CHANGELOG.md') -Raw
$entry = [regex]::Match($changelog,'(?ms)^## ' + [regex]::Escape($Version) + '\s*\n(.*?)(?=^## |\z)').Groups[1].Value.Trim()
if (-not $entry) { throw 'Missing public changelog entry for this release' }
$notes = @"
SumatraPDF Enhanced $Version

$entry

## Downloads

- x64 installer EXE and standalone portable EXE: for Intel and AMD computers.
- ARM64 installer EXE and standalone portable EXE: for ARM devices.
- Portable ZIPs include the reader, dictionaries, license notices and optional shell helpers.
- Architecture-specific build manifests and SHA256SUMS files record package hashes and source provenance.

Run a portable EXE directly. The offline dictionary is embedded. Run an installer EXE to install; extract the entire folder when using a ZIP. Optional browser integrations require the separate WebView2 runtime. ARM64 is cross-compiled; real ARM64 hardware acceptance remains separate.

Authenticode status: $($signing -join '; '). Unsigned files have no publisher signature; SHA-256 hashes establish package integrity, not a malware-free guarantee.

## Upstream source

Based on **SumatraPDF $($upstream.version)** at upstream commit [$($upstream.commit)](https://github.com/sumatrapdfreader/sumatrapdf/commit/$($upstream.commit)), dated **$($upstream.commitDateUtc)**. This is the upstream commit date.

Enhanced source: [$SourceCommit](https://github.com/abelokoj/sumatrapdf-enhanced/commit/$SourceCommit).

Bundled fonts, icons and dictionary data retain their separate licenses. Notices are included in the application payload and source repository.
"@
$notesRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$notesPath = Join-Path $notesRoot 'enhanced-public-release-notes.md'
[IO.File]::WriteAllText($notesPath,$notes)
$tag = "enhanced-$Version"
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
# A draft need not create its Git ref; an existing ref must never be retargeted.
& gh api --method POST "repos/$env:GH_REPO/git/refs" -f "ref=refs/tags/$tag" -f "sha=$SourceCommit" 2>$null | Out-Null
$tagJson = & gh api "repos/$env:GH_REPO/git/ref/tags/$tag"
if ($LASTEXITCODE -ne 0) { throw 'Unable to verify release tag source; draft remains unpublished' }
$tagObject = ($tagJson | ConvertFrom-Json).object
$tagDepth = 0
while ($tagObject.type -eq 'tag' -and $tagDepth -lt 5) {
    $tagJson = & gh api "repos/$env:GH_REPO/git/tags/$($tagObject.sha)"
    if ($LASTEXITCODE -ne 0) { throw 'Unable to verify annotated release tag' }
    $tagObject = ($tagJson | ConvertFrom-Json).object
    $tagDepth++
}
if ($tagObject.type -ne 'commit' -or $tagObject.sha -ne $SourceCommit) { throw 'Release tag belongs to different source; draft remains unpublished' }
$releaseJson = & gh api "repos/$env:GH_REPO/releases?per_page=100"
if ($LASTEXITCODE -ne 0) { throw 'Unable to verify uploaded release files' }
$release = @($releaseJson | ConvertFrom-Json | Where-Object { $_.tag_name -eq $tag })
if ($release.Count -ne 1 -or -not $release[0].draft -or $release[0].target_commitish -ne $SourceCommit) { throw 'Expected one source-matched unpublished draft' }
$release = $release[0]
if ($release.assets.Count -ne $files.Count) { throw 'Unexpected uploaded release asset count' }
foreach ($file in $files) {
    $name = Split-Path $file -Leaf
    $asset = @($release.assets | Where-Object { $_.name -eq $name })
    $digest = 'sha256:' + (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()
    if ($asset.Count -ne 1 -or $asset[0].digest -ne $digest) { throw "Upload checksum mismatch: $name" }
}
& gh release edit $tag --draft=false --latest
if ($LASTEXITCODE -ne 0) { throw 'Unable to publish completed draft release' }
Write-Output "Published $tag with six app packages and four provenance files."
