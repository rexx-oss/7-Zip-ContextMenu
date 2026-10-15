# Must run as Administrator
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "Please run PowerShell as Administrator!"
    Exit
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if (-not $ScriptDir) { $ScriptDir = $PSScriptRoot }

Write-Host "=============================================" -ForegroundColor DarkCyan
Write-Host "        7zip-contextmenu Installer           " -ForegroundColor White
Write-Host "=============================================" -ForegroundColor DarkCyan

# Prompt user for Hash options
Write-Host "`nConfigure Checksum (CRC / SHA) options:" -ForegroundColor Yellow
$hashAns = Read-Host "Do you want to show CRC / SHA options in the context menu? (Y/N) [Default: Y]"

$hashMask = 0
if ($hashAns -notmatch '^[Nn]') {
    Write-Host "`nChoose which algorithms to display:" -ForegroundColor Cyan
    Write-Host "  1: CRC-32"
    Write-Host "  2: CRC-64"
    Write-Host "  3: SHA-1"
    Write-Host "  4: SHA-256"
    Write-Host "  5: *"
    Write-Host "  [Press Enter to enable All 5, or type numbers like 1,4,5]:" -ForegroundColor DarkGray
    $sel = Read-Host "Choice [Default: 1,2,3,4,5]"
    
    if ([string]::IsNullOrWhiteSpace($sel)) {
        $hashMask = 31 # (1|2|4|8|16) = All 5 enabled
    } else {
        $nums = $sel -split '[, ]+'
        if ($nums -contains '1') { $hashMask = $hashMask -bor 1 }
        if ($nums -contains '2') { $hashMask = $hashMask -bor 2 }
        if ($nums -contains '3') { $hashMask = $hashMask -bor 4 }
        if ($nums -contains '4') { $hashMask = $hashMask -bor 8 }
        if ($nums -contains '5') { $hashMask = $hashMask -bor 16 }
    }
}

# Save configuration for the DLL
reg add "HKCU\Software\7zip-contextmenu" /v "HashMask" /t REG_DWORD /d $hashMask /f | Out-Null

Write-Host "`n1. Installing Certificate..." -ForegroundColor Cyan
$certPath = Join-Path $ScriptDir "7zip-contextmenu.cer"
Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null

Write-Host "2. Installing directly to WindowsApps..." -ForegroundColor Cyan
$msixPath = Join-Path $ScriptDir "SevenZipMenu.msix"
Add-AppxPackage -Path $msixPath

Write-Host "3. Restarting Explorer..." -ForegroundColor Cyan
taskkill /f /im explorer.exe 2>$null
Start-Process explorer.exe

Write-Host "`nInstalled cleanly inside C:\Program Files\WindowsApps\7zip-contextmenu_...!" -ForegroundColor Green
Write-Host "You can now safely delete the downloaded folder." -ForegroundColor Yellow
