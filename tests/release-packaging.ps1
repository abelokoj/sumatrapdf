$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'enhanced-version.txt') -Raw).Trim()
$source = '1234567890123456789012345678901234567890'
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempRoot ('sumatra-release-tests-' + [Guid]::NewGuid())))
if (-not $testRoot.StartsWith($tempRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar)) { throw 'Invalid test directory' }
New-Item -ItemType Directory -Path $testRoot | Out-Null
$originalRunnerTemp = $env:RUNNER_TEMP
$env:RUNNER_TEMP = Join-Path $testRoot 'notes'
New-Item -ItemType Directory -Path $env:RUNNER_TEMP | Out-Null
$global:ReleasePackagingTestCommands = [Collections.Generic.List[object]]::new()
function gh {
    if ($args[0] -eq 'release' -and $args[1] -eq 'view') { $global:LASTEXITCODE = 1; return }
    if ($args[0] -eq 'api') {
        $assets = @(Get-ChildItem -LiteralPath $testRoot -File | Where-Object { $_.Extension -in @('.exe','.zip') } | ForEach-Object {
            @{ name = $_.Name; digest = 'sha256:' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        })
        $global:LASTEXITCODE = 0
        if ($args[1] -notmatch '/releases\?per_page=100$') { throw 'Drafts must be read from the release list, not the published-tag endpoint' }
        return (ConvertTo-Json -InputObject @(@{ tag_name = "enhanced-$version"; draft = $true; assets = $assets }) -Depth 4)
    }
    $global:ReleasePackagingTestCommands.Add(@($args)); $global:LASTEXITCODE = 0
}
function Expect-Rejection([string]$pattern) {
    $rejected = $false
    try { & (Join-Path $repoRoot 'cmd/publish-enhanced.ps1') -Version $version -PackageDirectory $testRoot -SourceCommit $source }
    catch { $rejected = $_.Exception.Message -match $pattern }
    if (-not $rejected -or $global:ReleasePackagingTestCommands.Count) { throw "Release guard failed: $pattern" }
}
try {
    Expect-Rejection 'Missing release package'
    foreach ($arch in @('x64','arm64')) {
        foreach ($kind in @('install.exe','portable.exe')) {
            $bytes = [byte[]]::new(256)
            [BitConverter]::GetBytes([int]128).CopyTo($bytes,0x3C)
            $machine = if ($arch -eq 'x64') { 0x8664 } else { 0xAA64 }
            [BitConverter]::GetBytes([UInt16]$machine).CopyTo($bytes,132)
            [IO.File]::WriteAllBytes((Join-Path $testRoot "SumatraPDF-Enhanced-$version-$arch-$kind"),$bytes)
        }
        $zip = [IO.Compression.ZipFile]::Open((Join-Path $testRoot "SumatraPDF-Enhanced-$version-$arch-portable.zip"),[IO.Compression.ZipArchiveMode]::Create)
        $zip.CreateEntry('app/README.txt') | Out-Null
        $zip.Dispose()
    }
    $armPath = Join-Path $testRoot "SumatraPDF-Enhanced-$version-arm64-portable.exe"
    $armBytes = [IO.File]::ReadAllBytes($armPath)
    $wrongBytes = $armBytes.Clone()
    [BitConverter]::GetBytes([UInt16]0x8664).CopyTo($wrongBytes,132)
    [IO.File]::WriteAllBytes($armPath,$wrongBytes)
    Expect-Rejection 'Incorrect architecture'
    [IO.File]::WriteAllBytes($armPath,$armBytes)
    $zipPath = Join-Path $testRoot "SumatraPDF-Enhanced-$version-x64-portable.zip"
    $zip = [IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Update)
    $zip.CreateEntry('app/PrettySumatraPDF_TODO.md') | Out-Null
    $zip.Dispose()
    Expect-Rejection 'Private record'
    $zip = [IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Update)
    $zip.GetEntry('app/PrettySumatraPDF_TODO.md').Delete()
    $zip.Dispose()
    & (Join-Path $repoRoot 'cmd/publish-enhanced.ps1') -Version $version -PackageDirectory $testRoot -SourceCommit $source
    if ($global:ReleasePackagingTestCommands.Count -ne 2) { throw 'Expected draft creation followed by publication' }
    $create = @($global:ReleasePackagingTestCommands[0])
    $edit = @($global:ReleasePackagingTestCommands[1])
    if ($create[1] -ne 'create' -or $create[2] -ne "enhanced-$version" -or $create -notcontains '--draft' -or $create -notcontains $source) { throw 'Incorrect release target' }
    if (@($create | Where-Object { $_ -match '.(exe|zip)$' }).Count -ne 6) { throw 'Expected six uploaded app packages' }
    if ($edit[1] -ne 'edit' -or $edit -notcontains '--draft=false') { throw 'Draft was not promoted after upload' }
    $notes = Get-Content -LiteralPath (Join-Path $env:RUNNER_TEMP 'enhanced-public-release-notes.md') -Raw
    if ($notes -notmatch 'a0d8bcaed0412ce803d9c213845e710fcbe3c7a5' -or $notes -notmatch '2026-09-30 09:55:54 UTC') { throw 'Missing upstream source/date' }
    Write-Output 'PASS: release guards, six packages, version tag, source date and draft publication.'
} finally {
    $env:RUNNER_TEMP = $originalRunnerTemp
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
