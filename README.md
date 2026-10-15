# 7-Zip-ContextMenu

Adds official 7-Zip to the Windows 11 modern context menu in a flat layout with dynamic target naming, customizable CRC/SHA options, and no duplicates in the classic menu.

## Prerequisites
Official 7-Zip installed (`C:\Program Files\7-Zip\`).

## Build (GitHub Actions)
1. Go to the Actions tab in your repository.
2. Select "Build 7-Zip-ContextMenu Package" -> click "Run workflow".
3. Download `7-Zip-ContextMenu.zip` from Artifacts.

## Install
1. Extract `7-Zip-ContextMenu.zip`.
2. Open PowerShell as Administrator in that folder:
   powershell -ExecutionPolicy Bypass -File .\install.ps1
3. Select which hash options to display when prompted (Enter for all, or N for none).
4. Delete the extracted folder after installation.

## Uninstall
Open PowerShell as Administrator in that folder:
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1
