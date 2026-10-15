# Must run as Administrator
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "Please run PowerShell as Administrator!"
    Exit
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if (-not $ScriptDir) { $ScriptDir = $PSScriptRoot }

Write-Host "=============================================" -ForegroundColor DarkCyan
Write-Host "       7-Zip Context Menu Installer          " -ForegroundColor White
Write-Host "=============================================" -ForegroundColor DarkCyan

Write-Host "1. Installing Certificate..." -ForegroundColor Cyan
$certPath = Join-Path $ScriptDir "7-Zip-ContextMenu.cer"
Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null

Write-Host "2. Installing directly to WindowsApps..." -ForegroundColor Cyan
$msixPath = Join-Path $ScriptDir "SevenZipMenu.msix"
Add-AppxPackage -Path $msixPath

Write-Host "3. Restarting Explorer..." -ForegroundColor Cyan
taskkill /f /im explorer.exe 2>$null
Start-Process explorer.exe

Write-Host "`nInstalled completely inside C:\Program Files\WindowsApps!" -ForegroundColor Green
Write-Host "You can now safely delete the downloaded folder." -ForegroundColor Yellow
