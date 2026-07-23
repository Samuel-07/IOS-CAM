# MiCam Pro - Troubleshooting & Fixes Log

#troubleshooting #fixes #bugs #cpp #ios #sideloadly #second-brain

This document records all errors encountered during the build, packaging, rendering, and sideloading process of MiCam Pro, along with their exact root causes and implemented solutions.

---

## 🔍 Issue 1: MSVC Unresolved External `main` Linker Error (`LNK2019`)

### ❌ Symptom
```text
MSVCRT.lib(exe_main.obj) : error LNK2019: simbolo externo main sin resolver
C:\Users\samue\Downloads\IOS CAM\build\Release\MiCamDesktop.exe : fatal error LNK1120: 1 externos sin resolver
```

### 🔬 Root Cause
CMake defaulted `MiCamDesktop` to a console application expecting `int main()`, while the application used `WinMain` as its entry point.

### ✅ Solution
1. Updated `CMakeLists.txt` to set the WIN32 GUI subsystem:
   ```cmake
   add_executable(MiCamDesktop WIN32
       Desktop/src/main.cpp
       Desktop/src/UIWindow.cpp
       Desktop/src/DeviceManager.cpp
       Desktop/src/SharedMemoryStream.cpp
   )
   ```
2. Defined standard `WinMain` entry point in `Desktop/src/main.cpp`:
   ```cpp
   int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) { ... }
   ```

---

## 🔍 Issue 2: Console Window Opening Alongside Desktop GUI

### ❌ Symptom
Running `MiCamDesktop.exe` opened a command prompt terminal window behind the GUI.

### ✅ Solution
By specifying `add_executable(MiCamDesktop WIN32 ...)` in `CMakeLists.txt`, MSVC targets the Windows Subsystem (`/SUBSYSTEM:WINDOWS`), completely suppressing terminal window instantiation upon execution.

---

## 🔍 Issue 3: GDI+ UTF-8 Text Encoding Mojibake Artifacts (`â€”`, `CÃ□MARA`, `ESPAÃ'OL`)

### ❌ Symptom
Spanish accented letters and UTF-8 characters rendered as garbled characters (e.g. `TRANSMISIÃ"N`, `BaterÃa`).

### 🔬 Root Cause
MSVC compiled string literals using ANSI (Windows-1252) codepage instead of UTF-8, causing multi-byte UTF-8 sequences to be interpreted as separate ANSI characters by GDI+.

### ✅ Solution
1. Added `/utf-8` compiler flag in `CMakeLists.txt`:
   ```cmake
   if(MSVC)
       add_compile_options(/utf-8)
   endif()
   ```
2. Cleaned up string literals in `Desktop/src/UIWindow.cpp` to use clear Unicode wchar_t strings (`L"ESPAÑOL"`, `L"TRANSMISION EN VIVO"`).

---

## 🔍 Issue 4: Sideloadly Missing Bundle ID Error (`NSLocalizedFailureReason=Missing bundle ID`)

### ❌ Symptom
```text
IXErrorDomain Code=13 "Failed to get bundle ID from Payload/MiCam.app"
NSLocalizedFailureReason=Missing bundle ID.
```

### 🔬 Root Cause
The zip archive placed `Info.plist` inside a nested subfolder instead of directly at `Payload/MiCam.app/Info.plist`.

### ✅ Solution
Updated `.github/workflows/ios-build.yml` to write `Info.plist` explicitly inside `build/Payload/MiCam.app/Info.plist` before executing `zip -r "$BUILD_ID.ipa" Payload`.

---

## 🔍 Issue 5: Sideloadly Platform Case Error (`Guru Meditation no default case defined`)

### ❌ Symptom
```text
Install failed: Guru Meditation 7c026a@832:1e99fb no default case defined
```

### 🔬 Root Cause
Sideloadly's parser requires explicit Apple platform identification keys in `Info.plist`: `CFBundleSupportedPlatforms` and `UIDeviceFamily`.

### ✅ Solution
Added canonical platform keys into `Info.plist` in `.github/workflows/ios-build.yml`:
```xml
<key>MinimumOSVersion</key>
<string>15.0</string>
<key>CFBundleSupportedPlatforms</key>
<array>
    <string>iPhoneOS</string>
</array>
<key>UIDeviceFamily</key>
<array>
    <integer>1</integer>
    <integer>2</integer>
</array>
```
Sideloadly now parses and installs `MiCam-Pro.ipa` cleanly in 1 click.
