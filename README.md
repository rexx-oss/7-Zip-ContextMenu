# 7-Zip-ContextMenu

Adds official 7-Zip to the Windows 11 top-level modern context menu in a flat, NanaZip-style layout with dynamic target naming and selectable checksum options.

## Prerequisites
Official 7-Zip installed (default: C:\Program Files\7-Zip).

## Build (GitHub Actions)
1. Go to Actions -> "Build 7-Zip-ContextMenu Package".
2. Click "Run workflow".
3. Download 7-Zip-ContextMenu.zip from Artifacts.

## Release (GitHub Actions)
1. Go to Actions -> "Release 7-Zip-ContextMenu".
2. Click "Run workflow" and enter the release tag (e.g. v1.0.0).

## Install
1. Extract 7-Zip-ContextMenu.zip.
2. Open PowerShell as Administrator in that folder:
   powershell -ExecutionPolicy Bypass -File .\install.ps1
3. Choose which checksum algorithms to show when prompted (Enter for all, or N for none).
4. Delete the extracted folder after installation.

## Uninstall
Open PowerShell as Administrator in that folder:
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1
