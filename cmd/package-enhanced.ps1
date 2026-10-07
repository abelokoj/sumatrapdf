param(
    [ValidateSet('x64','arm64')][string]$Architecture = 'x64',
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../enhanced-version.txt') -Raw).Trim()
)
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path $PSScriptRoot -Parent
$binaryDir = Join-Path $repoDir $(if ($Architecture -eq 'arm64') { 'out/arm64' } else { 'out/rel64' })
$stageDir = Join-Path $repoDir "dist/stage-$Architecture/SumatraPDF-Enhanced-$Version"
$expectedStage = [IO.Path]::GetFullPath((Join-Path $repoDir "dist/stage-$Architecture"))
$resolvedStage = [IO.Path]::GetFullPath($stageDir)
if (-not $resolvedStage.StartsWith($expectedStage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid packaging stage' }
if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
$sourceSha = (& git -C $repoDir rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceSha -notmatch '^[0-9a-f]{40}$') { throw 'Unable to identify source commit' }
$sourceDirty = [bool](& git -C $repoDir status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Unable to identify source status' }
if ($env:GITHUB_ACTIONS -eq 'true' -and $sourceDirty) { throw 'Hosted packages require a clean source checkout' }
$upstream = Get-Content -LiteralPath (Join-Path $repoDir 'enhanced-upstream.json') -Raw | ConvertFrom-Json
$machine = if ($Architecture -eq 'arm64') { 0xAA64 } else { 0x8664 }
$signatures = @()
foreach ($name in @('SumatraPDF.exe','libsumatrapdf.dll','PdfFilter.dll','PdfPreview.dll','sumatrapdf-tool.exe','SumatraPDF-static.exe')) {
    $source = Join-Path $binaryDir $name
    $bytes = [IO.File]::ReadAllBytes($source)
    if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes,0) -ne 0x5A4D) { throw "Invalid PE binary: $name" }
    $offset = [BitConverter]::ToInt32($bytes,0x3C)
    if ($offset -lt 64 -or $offset -gt $bytes.Length - 24 -or [BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550) { throw "Invalid PE header: $name" }
    if ([BitConverter]::ToUInt16($bytes,$offset + 4) -ne $machine) { throw "Wrong binary architecture: $name" }
    if ($name -in @('SumatraPDF.exe','SumatraPDF-static.exe')) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($source)
        if ($info.ProductName -ne 'SumatraPDF Enhanced' -or $info.ProductVersion -ne $Version.TrimStart('v')) { throw "Product identity/version mismatch: $name" }
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $source
    if ($signature.Status -notin @('Valid','NotSigned')) { throw "Invalid Authenticode status: $name ($($signature.Status))" }
    $signatures += @{ name = $name; status = $signature.Status.ToString(); signerThumbprint = $(if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { $null }) }
    if ($name -ne 'SumatraPDF-static.exe') { Copy-Item -LiteralPath $source -Destination $stageDir }
}
foreach ($name in @('AUTHORS','COPYING','COPYING.BSD')) { Copy-Item -LiteralPath (Join-Path $repoDir $name) -Destination $stageDir }
Copy-Item -LiteralPath (Join-Path $repoDir 'src/dictionaries') -Destination (Join-Path $stageDir 'dictionaries') -Recurse
$docsDir = Join-Path $stageDir 'docs'
New-Item -ItemType Directory -Force -Path $docsDir | Out-Null
foreach ($name in @('licenses','dictionaries','font-attribution.md','icon-attribution.md','vocabulary-attribution.md')) {
    Copy-Item -LiteralPath (Join-Path $repoDir "docs/$name") -Destination (Join-Path $docsDir $name) -Recurse
}
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir 'data') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoDir 'data/vocabulary') -Destination (Join-Path $stageDir 'data/vocabulary') -Recurse
$signing = if (@($signatures | Where-Object { $_.status -eq 'NotSigned' }).Count -eq $signatures.Count) { 'unsigned' } elseif (@($signatures | Where-Object { $_.status -eq 'Valid' }).Count -eq $signatures.Count) { 'signed' } else { 'mixed' }
@"
SumatraPDF Enhanced $Version - Windows $Architecture
Extract the entire folder and run SumatraPDF.exe. Keep dictionaries beside it.
Select a word and press Shift+D for offline dictionary lookup.
The home learning hub opens vocabulary and practice activities.
Release information: https://github.com/abelokoj/sumatrapdf-enhanced/releases/latest
Source commit: $sourceSha
Original SumatraPDF: $($upstream.version), $($upstream.commit).
Authenticode status: $signing. See BUILD_MANIFEST.json for individual files.
"@ | Set-Content -LiteralPath (Join-Path $stageDir 'README.txt') -Encoding utf8
@"
Original SumatraPDF version: $($upstream.version)
Upstream commit: $($upstream.commit)
Upstream source snapshot date: $($upstream.commitDateUtc)
Enhanced source commit: $sourceSha
Architecture: $Architecture
Build environment: $($env:RUNNER_ENVIRONMENT)
"@ | Set-Content -LiteralPath (Join-Path $stageDir 'UPSTREAM_BASE.txt') -Encoding utf8
@"
Theme = Sumatra Light
CheckForUpdates = false
RememberOpenedFiles = true
UseTabs = true
ReuseInstance = false
UIFontFamily = system
"@ | Set-Content -LiteralPath (Join-Path $stageDir 'SumatraPDFEnhanced-settings.txt') -Encoding utf8
$payloadFiles = @(Get-ChildItem -LiteralPath $stageDir -File -Recurse | Sort-Object FullName | ForEach-Object {
    @{ path = [IO.Path]::GetRelativePath($stageDir,$_.FullName).Replace('\','/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
})
$toolchain = $null
$bunVersion = $null
$bunCommand = Get-Command bun -ErrorAction SilentlyContinue
if ($bunCommand) { $bunVersion = & $bunCommand.Source --version }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        $cl = @(Get-ChildItem -LiteralPath (Join-Path $vsPath 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'bin/Hostx64/x64/cl.exe' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1)
        if ($cl.Count) { $toolchain = [Diagnostics.FileVersionInfo]::GetVersionInfo($cl[0]).FileVersion }
    }
}
$manifest = [ordered]@{
    schema = 1; version = $Version; architecture = $Architecture; sourceCommit = $sourceSha; sourceDirty = $sourceDirty
    upstream = $upstream; signing = $signing; signatures = $signatures
    build = @{ configuration = 'Release'; runner = $env:RUNNER_ENVIRONMENT; os = [Environment]::OSVersion.VersionString; compiler = $toolchain; powershell = $PSVersionTable.PSVersion.ToString(); bun = $bunVersion; manualIncluded = (Test-Path -LiteralPath (Join-Path $repoDir '.work/docs/manifest.json')) }
    files = $payloadFiles
}
$payloadManifest = Join-Path $stageDir 'BUILD_MANIFEST.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $payloadManifest -Encoding utf8
$archive = Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-portable.zip"
Compress-Archive -Path $stageDir -DestinationPath $archive -Force
$installer = Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-install.exe"
Copy-Item -LiteralPath (Join-Path $binaryDir 'SumatraPDF.exe') -Destination $installer
$portable = Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-portable.exe"
Copy-Item -LiteralPath (Join-Path $binaryDir 'SumatraPDF-static.exe') -Destination $portable
$manifest['payloadManifestSha256'] = (Get-FileHash -LiteralPath $payloadManifest).Hash.ToLowerInvariant()
$manifest['packages'] = @(@($installer,$portable,$archive) | ForEach-Object {
    $file = Get-Item -LiteralPath $_
    @{ name = $file.Name; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant() }
})
$manifestPath = Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$checksums = @(@($installer,$portable,$archive,$manifestPath) | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant() + '  ' + (Split-Path $_ -Leaf) })
$checksums | Set-Content -LiteralPath (Join-Path $repoDir "dist/SumatraPDF-Enhanced-$Version-$Architecture-SHA256SUMS.txt") -Encoding utf8
Write-Output $portable
Write-Output $archive
Write-Output $installer
Write-Output $manifestPath
