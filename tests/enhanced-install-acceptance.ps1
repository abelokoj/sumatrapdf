param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$PortableZip,
    [Parameter(Mandatory)][string]$OfficialInstaller,
    [Parameter(Mandatory)][string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
$hosted = $env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_ENVIRONMENT -eq 'github-hosted'
$sandbox = $env:USERNAME -eq 'WDAGUtilityAccount' -and (Test-Path -LiteralPath 'C:\EnhancedAcceptance\sandbox-marker.txt')
if (-not ($hosted -or $sandbox)) { throw 'Installer acceptance may run only in Windows Sandbox or a disposable GitHub-hosted VM' }
foreach ($path in @($Installer,$PortableZip,$OfficialInstaller)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing acceptance input: $path" }
}
if ([Diagnostics.FileVersionInfo]::GetVersionInfo([IO.Path]::GetFullPath($Installer)).ProductName -ne 'SumatraPDF Enhanced') { throw 'Wrong Enhanced installer product' }
if ([Diagnostics.FileVersionInfo]::GetVersionInfo([IO.Path]::GetFullPath($OfficialInstaller)).ProductName -ne 'SumatraPDF') { throw 'Wrong official installer product' }
$reportRoot = [IO.Path]::GetFullPath($ReportDirectory)
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempRoot ('enhanced-install-acceptance-' + [Guid]::NewGuid())))
if (-not $testRoot.StartsWith($tempRoot.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid acceptance temp root' }
New-Item -ItemType Directory -Path $testRoot | Out-Null
$results = [Collections.Generic.List[object]]::new()
function Invoke-Exe([string]$exe,[string[]]$arguments,[int]$timeout = 120) {
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit($timeout * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw "Acceptance process timed out: $exe"
    }
    if ($process.ExitCode -ne 0) { throw "Acceptance process failed: $exe ($($process.ExitCode))" }
}
function File-Snapshot([string]$directory) {
    @((Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($directory.TrimEnd('\').Length + 1) + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash
    })) -join "`n"
}
function Reg-Snapshot([string]$root,[string[]]$paths) {
    $snapshot = @(foreach ($path in $paths) {
        $key = Join-Path $root $path
        if (-not (Test-Path -LiteralPath $key)) { "$path=<missing>"; continue }
        $keys = @((Get-Item -LiteralPath $key)) + @(Get-ChildItem -LiteralPath $key -Recurse)
        foreach ($item in $keys | Sort-Object Name) {
            foreach ($name in $item.GetValueNames() | Sort-Object) {
                "$($item.Name):$name=" + (ConvertTo-Json -InputObject $item.GetValue($name) -Compress)
            }
        }
    })
    $snapshot -join "`n"
}
function Registry-State([string[]]$paths) {
    (Reg-Snapshot 'HKCU:' $paths) + "`n" + (Reg-Snapshot 'HKLM:' $paths)
}
function Shortcut-State {
    $paths = @('DesktopDirectory','CommonDesktopDirectory','Programs','CommonPrograms')
    @(foreach ($folder in $paths) {
        $directory = [Environment]::GetFolderPath($folder)
        if (-not $directory) { continue }
        $path = Join-Path $directory 'SumatraPDF.lnk'
        if (Test-Path -LiteralPath $path) { $path + ':' + (Get-FileHash -LiteralPath $path).Hash } else { $path + ':<missing>' }
    }) -join "`n"
}
$officialKeys = @('Software\SumatraPDF','Software\Microsoft\Windows\CurrentVersion\Uninstall\SumatraPDF','Software\Microsoft\Windows\CurrentVersion\App Paths\SumatraPDF.exe','Software\Classes\SumatraPDF.pdf','Software\Classes\Applications\SumatraPDF.exe')
$providerKeys = @('Software\Classes\.pdf\shellex\{8895b1c6-b41f-4c1c-a562-0d564250836f}','Software\Classes\.pdf\PersistentHandler')
$enhancedUninstall = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\SumatraPDF Enhanced'
try {
    $pdf = Join-Path $testRoot 'smoke.pdf'
    [IO.File]::WriteAllText($pdf,"%PDF-1.4`n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj`n2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj`n3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]/Resources<<>>>>endobj`ntrailer<</Root 1 0 R>>`n%%EOF",[Text.Encoding]::ASCII)
    $portableDir = Join-Path $testRoot 'portable'
    Expand-Archive -LiteralPath $PortableZip -DestinationPath $portableDir
    $portableExe = @(Get-ChildItem -LiteralPath $portableDir -Filter 'SumatraPDF.exe' -File -Recurse)
    if ($portableExe.Count -ne 1) { throw 'Missing extracted portable reader' }
    Invoke-Exe $portableExe[0].FullName @('-for-testing','-bench',('"' + $pdf + '"'))
    $results.Add(@{ check = 'clean extracted portable benchmark'; status = 'passed' })
    foreach ($scope in @('per-user','all-user')) {
        $allUsers = $scope -eq 'all-user'
        $root = if ($allUsers) { 'HKLM:' } else { 'HKCU:' }
        $scopeFlags = if ($allUsers) { @('-all-users') } else { @() }
        $officialDir = Join-Path $testRoot "official-$scope"
        $enhancedDir = Join-Path $testRoot "enhanced-$scope"
        Invoke-Exe $OfficialInstaller (@('-install','-silent','-install-dir',('"' + $officialDir + '"'),'-with-preview','-with-filter') + $scopeFlags)
        $officialExe = Join-Path $officialDir 'SumatraPDF.exe'
        if (-not (Test-Path -LiteralPath $officialExe)) { throw 'Official installation did not complete' }
        $officialFiles = File-Snapshot $officialDir
        $officialReg = Registry-State $officialKeys
        $providers = Registry-State $providerKeys
        $shortcuts = Shortcut-State
        $officialData = Join-Path $env:APPDATA 'SumatraPDF'
        New-Item -ItemType Directory -Force -Path $officialData | Out-Null
        $sentinel = Join-Path $officialData 'enhanced-acceptance-sentinel.txt'
        Set-Content -LiteralPath $sentinel -Value 'Official SumatraPDF data must survive Enhanced removal'
        $sentinelHash = (Get-FileHash -LiteralPath $sentinel).Hash
        $officialDataFiles = File-Snapshot $officialData
        foreach ($phase in @('install','upgrade')) {
            Invoke-Exe $Installer (@('-for-testing','-install','-silent','-install-dir',('"' + $enhancedDir + '"'),'-with-preview','-with-filter') + $scopeFlags)
            $exe = Join-Path $enhancedDir 'SumatraPDFEnhanced.exe'
            if (-not (Test-Path -LiteralPath $exe)) { throw "Enhanced $phase did not complete" }
            $registration = Get-ItemProperty -LiteralPath (Join-Path $root $enhancedUninstall)
            if ($registration.DisplayName -notlike 'SumatraPDF Enhanced*' -or $registration.InstallLocation -ne $enhancedDir) { throw 'Incorrect Enhanced uninstall registration' }
            if ($officialFiles -ne (File-Snapshot $officialDir) -or $officialReg -ne (Registry-State $officialKeys) -or $providers -ne (Registry-State $providerKeys) -or $shortcuts -ne (Shortcut-State) -or $officialDataFiles -ne (File-Snapshot $officialData)) { throw "Enhanced $phase changed official installation/data/shortcuts/provider ownership" }
            Invoke-Exe $exe @('-for-testing','-bench',('"' + $pdf + '"'))
            $results.Add(@{ check = "$scope $phase, branded registration, launch and provider preservation"; status = 'passed' })
        }
        Invoke-Exe $exe (@('-for-testing','-uninstall','-silent') + $scopeFlags)
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while ((Test-Path -LiteralPath $exe) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
        if ((Test-Path -LiteralPath $exe) -or (Test-Path -LiteralPath (Join-Path $root $enhancedUninstall))) { throw 'Enhanced uninstall left app or registration' }
        if ($officialFiles -ne (File-Snapshot $officialDir) -or $officialReg -ne (Registry-State $officialKeys) -or $providers -ne (Registry-State $providerKeys) -or $shortcuts -ne (Shortcut-State) -or $officialDataFiles -ne (File-Snapshot $officialData) -or $sentinelHash -ne (Get-FileHash -LiteralPath $sentinel).Hash) { throw 'Enhanced uninstall changed official app, data, shortcuts or providers' }
        Invoke-Exe $officialExe @('-for-testing','-bench',('"' + $pdf + '"'))
        $results.Add(@{ check = "$scope uninstall preserves official app/data/providers and official launch"; status = 'passed' })
        Invoke-Exe $officialExe (@('-uninstall','-silent') + $scopeFlags)
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while ((Test-Path -LiteralPath $officialExe) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    }
    $status = 'passed'
} catch {
    $status = 'failed'
    $results.Add(@{ check = 'acceptance'; status = 'failed'; error = $_.Exception.Message })
    throw
} finally {
    @{ status = $status; disposableEnvironment = $(if ($hosted) { 'github-hosted' } else { 'Windows Sandbox' }); architecture = 'x64'; installerSha256 = (Get-FileHash -LiteralPath $Installer).Hash.ToLowerInvariant(); officialInstallerSha256 = (Get-FileHash -LiteralPath $OfficialInstaller).Hash.ToLowerInvariant(); checks = @($results.ToArray()); remaining = @('Explorer preview/Search rendering','physical ARM64 runtime','user-selected update/download dialogs') } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $reportRoot 'installer-acceptance.json') -Encoding utf8
    if ($status -eq 'passed') { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
