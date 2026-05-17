# .ora (*OpenRaster*) Windows Thumbnail Provider

## Contents
- [.ora (*OpenRaster*) Windows Thumbnail Provider](#ora-openraster-windows-thumbnail-provider)
  - [Contents](#contents)
  - [Requirements](#requirements)
  - [Build](#build)
  - [Install](#install)
  - [Uninstall](#uninstall)
  - [Notes](#notes)


## Requirements

- [*Visual Studio Build Tools*](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2026) with "*Desktop development with C++*" workload

## Build

From an *x64 Native Tools Command Prompt*:

```cmd
cl /LD /EHsc /O2 OraThumbnailProvider.cpp third_party\miniz\miniz.c /link /DEF:OraThumbnailProvider.def ole32.lib oleaut32.lib shlwapi.lib gdiplus.lib advapi32.lib shell32.lib /OUT:OraThumbnailProvider.dll
```

## Install

1. Register the DLL from an elevated prompt:

```cmd
regsvr32 G:\path\to\OraThumbnailProvider.dll
```

2. Register the thumbnail handler for the `.ora` file type (the `DllRegisterServer` only registers the CLSID; the file association must be added separately):

```cmd
reg add "HKCR\ora_auto_file\shellex\{e357fccd-a995-4576-b01f-234630154e96}" /ve /t REG_SZ /d "{8F6A1D3E-2C4B-4F7A-9E0D-1B5C3A8F2E6D}" /f
```

3. Clear the thumbnail cache and restart Explorer:

cmd:
```cmd
taskkill /f /im explorer.exe
del /f "%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.db"
start explorer
```

PowerShell:
```powershell
taskkill /f /im explorer.exe
Remove-Item "$env:LocalAppData\Microsoft\Windows\Explorer\thumbcache_*.db" -Force
Start-Process explorer
```

## Uninstall

```cmd
regsvr32 /u OraThumbnailProvider.dll
reg delete "HKCR\ora_auto_file\shellex\{e357fccd-a995-4576-b01f-234630154e96}" /f
```

## Notes

- Must be built as x64 - 64-bit Explorer will not load a 32-bit shell extension.
