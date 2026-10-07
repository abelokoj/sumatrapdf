param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$PortableZip,
    [Parameter(Mandatory)][string]$OfficialInstaller,
    [switch]$Launch
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$stage = Join-Path ([IO.Path]::GetTempPath()) ('sumatra-enhanced-sandbox-' + [Guid]::NewGuid())
$inputDir = Join-Path $stage 'input'
$outputDir = Join-Path $stage 'results'
New-Item -ItemType Directory -Path $inputDir,$outputDir | Out-Null
Copy-Item -LiteralPath $Installer -Destination (Join-Path $inputDir 'enhanced-install.exe')
Copy-Item -LiteralPath $PortableZip -Destination (Join-Path $inputDir 'enhanced-portable.zip')
Copy-Item -LiteralPath $OfficialInstaller -Destination (Join-Path $inputDir 'official-install.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'tests/enhanced-install-acceptance.ps1') -Destination $inputDir
Set-Content -LiteralPath (Join-Path $inputDir 'sandbox-marker.txt') -Value 'Disposable Enhanced installation acceptance'
$escapedInput = [Security.SecurityElement]::Escape($inputDir)
$escapedOutput = [Security.SecurityElement]::Escape($outputDir)
$config = Join-Path $stage 'enhanced-acceptance.wsb'
@"
<Configuration>
  <Networking>Disable</Networking>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <AudioInput>Disable</AudioInput>
  <VideoInput>Disable</VideoInput>
  <PrinterRedirection>Disable</PrinterRedirection>
  <MappedFolders>
    <MappedFolder><HostFolder>$escapedInput</HostFolder><SandboxFolder>C:\EnhancedAcceptance</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$escapedOutput</HostFolder><SandboxFolder>C:\EnhancedResults</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\EnhancedAcceptance\enhanced-install-acceptance.ps1 -Installer C:\EnhancedAcceptance\enhanced-install.exe -PortableZip C:\EnhancedAcceptance\enhanced-portable.zip -OfficialInstaller C:\EnhancedAcceptance\official-install.exe -ReportDirectory C:\EnhancedResults</Command></LogonCommand>
</Configuration>
"@ | Set-Content -LiteralPath $config -Encoding utf8
Write-Output "Prepared: $config"
Write-Output "Results: $outputDir"
if ($Launch) {
    $sandbox = Get-Command WindowsSandbox.exe -ErrorAction SilentlyContinue
    if (-not $sandbox) { throw "Windows Sandbox unavailable; configuration retained at $config" }
    Start-Process -FilePath $sandbox.Source -ArgumentList ('"' + $config + '"') -WindowStyle Hidden
}
