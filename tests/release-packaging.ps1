$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'enhanced-version.txt') -Raw).Trim()
$upstream = Get-Content -LiteralPath (Join-Path $repoRoot 'enhanced-upstream.json') -Raw | ConvertFrom-Json
$source = '1234567890123456789012345678901234567890'
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempRoot ('sumatra-release-tests-' + [Guid]::NewGuid())))
if (-not $testRoot.StartsWith($tempRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar)) { throw 'Invalid test directory' }
New-Item -ItemType Directory -Path $testRoot | Out-Null
$savedRunnerTemp = $env:RUNNER_TEMP
$savedRepo = $env:GH_REPO
$env:RUNNER_TEMP = Join-Path $testRoot 'notes'
$env:GH_REPO = 'abelokoj/sumatrapdf-enhanced'
New-Item -ItemType Directory -Path $env:RUNNER_TEMP | Out-Null
$global:ReleasePackagingTestCommands = [Collections.Generic.List[object]]::new()
$global:ReleasePackagingWrongTag = $false
function gh {
    if ($args[0] -eq 'release' -and $args[1] -eq 'view') { $global:LASTEXITCODE = 1; return }
    if ($args[0] -eq 'api') {
        if ($args -contains 'POST') { $global:ReleasePackagingTestCommands.Add(@($args)); $global:LASTEXITCODE = 0; return }
        if ($args[1] -match '/git/ref/tags/') { $global:LASTEXITCODE = 0; return (@{ object = @{ type = 'commit'; sha = $(if ($global:ReleasePackagingWrongTag) { 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' } else { $source }) } } | ConvertTo-Json) }
        $assets = @(Get-ChildItem -LiteralPath $testRoot -File | ForEach-Object {
            @{ name = $_.Name; digest = 'sha256:' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        })
        $global:LASTEXITCODE = 0
        return (ConvertTo-Json -InputObject @(@{ tag_name = "enhanced-$version"; target_commitish = $source; draft = $true; assets = $assets }) -Depth 5)
    }
    $global:ReleasePackagingTestCommands.Add(@($args))
    $global:LASTEXITCODE = 0
}
function Invoke-Publisher {
    & (Join-Path $repoRoot 'cmd/publish-enhanced.ps1') -Version $version -PackageDirectory $testRoot -SourceCommit $source
}
function Expect-Rejection([string]$pattern) {
    $global:ReleasePackagingTestCommands.Clear()
    $rejected = $false
    try { Invoke-Publisher } catch { $rejected = $_.Exception.Message -match $pattern }
    if (-not $rejected -or $global:ReleasePackagingTestCommands.Count) { throw "Release guard failed: $pattern" }
}
function Write-Manifests {
    foreach ($arch in @('x64','arm64')) {
        $packages = @(foreach ($kind in @('install.exe','portable.exe','portable.zip')) {
            $file = Get-Item -LiteralPath (Join-Path $testRoot "SumatraPDF-Enhanced-$version-$arch-$kind")
            @{ name = $file.Name; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() }
        })
        $manifest = @{ schema = 1; version = $version; architecture = $arch; sourceCommit = $source; sourceDirty = $false; upstream = $upstream; signing = 'unsigned'; packages = $packages }
        $name = "SumatraPDF-Enhanced-$version-$arch-manifest.json"
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $testRoot $name) -Encoding utf8
        $sums = @($packages | ForEach-Object { "$($_.sha256)  $($_.name)" })
        $sums += (Get-FileHash -LiteralPath (Join-Path $testRoot $name)).Hash.ToLowerInvariant() + "  $name"
        $sums | Set-Content -LiteralPath (Join-Path $testRoot "SumatraPDF-Enhanced-$version-$arch-SHA256SUMS.txt") -Encoding utf8
    }
}
try {
    Expect-Rejection 'Missing release package'
    foreach ($arch in @('x64','arm64')) {
        $bytes = [byte[]]::new(256)
        [BitConverter]::GetBytes([UInt16]0x5A4D).CopyTo($bytes,0)
        [BitConverter]::GetBytes([int]128).CopyTo($bytes,0x3C)
        [BitConverter]::GetBytes([UInt32]0x4550).CopyTo($bytes,128)
        $machine = if ($arch -eq 'x64') { 0x8664 } else { 0xAA64 }
        [BitConverter]::GetBytes([UInt16]$machine).CopyTo($bytes,132)
        foreach ($kind in @('install.exe','portable.exe')) {
            [IO.File]::WriteAllBytes((Join-Path $testRoot "SumatraPDF-Enhanced-$version-$arch-$kind"),$bytes)
        }
        $zip = [IO.Compression.ZipFile]::Open((Join-Path $testRoot "SumatraPDF-Enhanced-$version-$arch-portable.zip"),[IO.Compression.ZipArchiveMode]::Create)
        try {
            $entry = $zip.CreateEntry('app/SumatraPDF.exe')
            $stream = $entry.Open(); $stream.Write($bytes,0,$bytes.Length); $stream.Dispose()
            $entry = $zip.CreateEntry('app/BUILD_MANIFEST.json')
            $writer = [IO.StreamWriter]::new($entry.Open())
            $payload = @{ schema = 1; version = $version; architecture = $arch; sourceCommit = $source; sourceDirty = $false; upstream = $upstream; signing = 'unsigned'; files = @(@{ path = 'SumatraPDF.exe'; bytes = $bytes.Length; sha256 = (Get-FileHash -LiteralPath (Join-Path $testRoot "SumatraPDF-Enhanced-$version-$arch-install.exe")).Hash.ToLowerInvariant() }) }
            $writer.Write(($payload | ConvertTo-Json -Depth 6)); $writer.Dispose()
        } finally { $zip.Dispose() }
    }
    Expect-Rejection 'Missing build manifest'
    Write-Manifests
    $armPath = Join-Path $testRoot "SumatraPDF-Enhanced-$version-arm64-portable.exe"
    $armBytes = [IO.File]::ReadAllBytes($armPath)
    $wrongBytes = $armBytes.Clone()
    [BitConverter]::GetBytes([UInt16]0x8664).CopyTo($wrongBytes,132)
    [IO.File]::WriteAllBytes($armPath,$wrongBytes); Write-Manifests
    Expect-Rejection 'Incorrect architecture'
    [IO.File]::WriteAllBytes($armPath,$armBytes)
    $zipPath = Join-Path $testRoot "SumatraPDF-Enhanced-$version-x64-portable.zip"
    $zip = [IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Update)
    $zip.CreateEntry('app/PrettySumatraPDF_TODO.md') | Out-Null; $zip.Dispose(); Write-Manifests
    Expect-Rejection 'Private record'
    $zip = [IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Update)
    $zip.GetEntry('app/PrettySumatraPDF_TODO.md').Delete()
    $zip.CreateEntry('app/private-development/session.md') | Out-Null; $zip.Dispose(); Write-Manifests
    Expect-Rejection 'Private record'
    $zip = [IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Update)
    $zip.GetEntry('app/private-development/session.md').Delete(); $zip.Dispose(); Write-Manifests
    $manifestPath = Join-Path $testRoot "SumatraPDF-Enhanced-$version-x64-manifest.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.sourceCommit = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Expect-Rejection 'Source provenance'
    Write-Manifests
    [IO.File]::WriteAllBytes($armPath,$wrongBytes)
    Expect-Rejection 'Incorrect architecture|Package checksum'
    [IO.File]::WriteAllBytes($armPath,$armBytes); Write-Manifests
    $env:GH_REPO = 'abelokoj/sumatrapdf'
    Expect-Rejection 'canonical Enhanced repository'
    $env:GH_REPO = 'abelokoj/sumatrapdf-enhanced'
    $global:ReleasePackagingWrongTag = $true
    $held = $false
    try { Invoke-Publisher } catch { $held = $_.Exception.Message -match 'Release tag belongs to different source' }
    if (-not $held -or @($global:ReleasePackagingTestCommands | Where-Object { $_[0] -eq 'release' -and $_[1] -eq 'edit' }).Count) { throw 'Wrong-source tag allowed draft publication' }
    $global:ReleasePackagingWrongTag = $false
    $global:ReleasePackagingTestCommands.Clear()
    Invoke-Publisher
    if ($global:ReleasePackagingTestCommands.Count -ne 3) { throw 'Expected draft creation, source tag and publication' }
    $create = @($global:ReleasePackagingTestCommands[0]); $tagCreate = @($global:ReleasePackagingTestCommands[1]); $edit = @($global:ReleasePackagingTestCommands[2])
    if ($create[1] -ne 'create' -or $create[2] -ne "enhanced-$version" -or $create -notcontains '--draft' -or $create -notcontains $source) { throw 'Incorrect release target' }
    if (@($create | Where-Object { $_ -match '\.(exe|zip|json|txt)$' }).Count -ne 10) { throw 'Expected six app packages and four provenance files' }
    if ($tagCreate -notcontains "sha=$source" -or $tagCreate -notcontains "ref=refs/tags/enhanced-$version") { throw 'Incorrect source tag' }
    if ($edit[1] -ne 'edit' -or $edit -notcontains '--draft=false') { throw 'Draft not promoted' }
    $notes = Get-Content -LiteralPath (Join-Path $env:RUNNER_TEMP 'enhanced-public-release-notes.md') -Raw
    if ($notes -notmatch [Regex]::Escape($upstream.commit) -or $notes -notmatch [Regex]::Escape($upstream.commitDateUtc) -or $notes -notmatch 'unsigned') { throw 'Missing upstream source/date or unsigned status' }
    Write-Output 'PASS: package/source/hash/privacy/repository guards and verified draft promotion.'
} finally {
    $env:RUNNER_TEMP = $savedRunnerTemp; $env:GH_REPO = $savedRepo
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
