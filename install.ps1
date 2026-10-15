# Must run as Administrator
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "Please run PowerShell as Administrator!"
    Exit
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Check if official 7-Zip is installed
$sevenZipExe = "C:\Program Files\7-Zip\7zFM.exe"
if (-not (Test-Path $sevenZipExe)) {
    $sevenZipExe = "C:\Program Files (x86)\7-Zip\7zFM.exe"
}
if (-not (Test-Path $sevenZipExe)) {
    Write-Error "Official 7-Zip was not found! Please install 7-Zip from https://7-zip.org first."
    Exit
}

Write-Host "1. Staging 7-Zip executable into package directory..." -ForegroundColor Cyan
Copy-Item $sevenZipExe -Destination (Join-Path $ScriptDir "7zFM.exe") -Force

Write-Host "2. Installing Security Certificate..." -ForegroundColor Cyan
$certPath = Join-Path $ScriptDir "Vanilla7ZipContextMenu.cer"
Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null

Write-Host "3. Registering 7-Zip Sparse Context Menu Package..." -ForegroundColor Cyan
$msixPath = Join-Path $ScriptDir "SevenZipMenu.msix"
Add-AppxPackage -Path $msixPath -ExternalLocation $ScriptDir

Write-Host "4. Restarting Explorer..." -ForegroundColor Cyan
taskkill /f /im explorer.exe 2>$null
Start-Process explorer.exe

Write-Host "`nAll done! '7-Zip >' cascading menu is now active in the modern context menu." -ForegroundColor Green
