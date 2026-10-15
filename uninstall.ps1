# Must run as Administrator
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "Please run PowerShell as Administrator!"
    Exit
}

Write-Host "1. Removing 7-Zip Context Menu Package..." -ForegroundColor Cyan
Get-AppxPackage 7-Zip-ContextMenu | Remove-AppxPackage -ErrorAction SilentlyContinue

Write-Host "2. Removing Security Certificate..." -ForegroundColor Cyan
$rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store("Root", "LocalMachine")
$rootStore.Open("ReadWrite")
$certs = $rootStore.Certificates | Where-Object { $_.Subject -like "*CN=7-Zip-ContextMenu*" }
foreach ($cert in $certs) {
    $rootStore.Remove($cert)
}
$rootStore.Close()

Write-Host "3. Restarting Explorer..." -ForegroundColor Cyan
taskkill /f /im explorer.exe 2>$null
Start-Process explorer.exe

Write-Host "`nUninstalled successfully! System reverted." -ForegroundColor Green
