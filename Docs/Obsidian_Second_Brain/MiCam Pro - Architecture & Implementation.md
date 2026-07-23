# MiCam Pro - Architecture & System Design

#software #ios #cpp #windows #obs #streaming #second-brain

## 📌 System Overview
**MiCam Pro** is a high-performance, ultra-low latency (< 24ms USB) personal alternative to DroidCam and Elgato Camera Hub. It transforms single or multiple iPhones into dedicated Windows Webcams (`Media Foundation Virtual Camera`) and native **OBS Studio** sources (`micam-obs-plugin`).

---

## 🏗️ Architecture Diagram

```
                            ┌─────────────────────────────┐
                            │    iOS Device (Swift App)   │
                            │  - AVFoundation (Dynamic)   │
                            │  - VideoToolbox (H.264/HEVC)│
                            │  - usbmuxd / mDNS Streamer  │
                            └──────────────┬──────────────┘
                                           │
                          USB (usbmuxd) / WiFi (mDNS + TCP)
                                           │
                            ┌──────────────┴──────────────┐
                            │  Windows Desktop App (C++)  │
                            │  - Device & Stream Manager  │
                            │  - Decoders & Telemetry     │
                            │  - Shared Memory Router     │
                            └───────┬─────────────┬───────┘
                                    │             │
                ┌───────────────────┴──┐       ┌──┴────────────────────┐
                │ Media Foundation /   │       │ OBS Studio Plugin     │
                │ DirectShow V-Cameras │       │ (Native C++ Source)   │
                │ (1, 2, 3...)         │       │                       │
                └──────────────────────┘       └───────────────────────┘
```

---

## 🛠️ Core Components & Tech Stack

### 1. iOS Native Application (`iOS/MiCam`)
- **Language**: Swift 5.9 / SwiftUI / AVFoundation / VideoToolbox.
- **Hardware Interrogation**: Dynamically scans camera lenses (Wide 1x, Ultra Wide 0.5x, Telephoto 3x, Front) and hardware format capabilities (720p, 1080p, 4K+ @ 24/30/60/120/240 FPS).
- **VideoToolbox Hardware Acceleration**: Direct `CVPixelBuffer` zero-copy H.264/HEVC compression with 0 B-frames for sub-50ms latency.
- **Dual Transport**:
  - **USB**: TCP port `50000` listening server forwarded via `usbmuxd`.
  - **WiFi**: `NWListener` service broadcasting via Bonjour (`_micam._tcp.local.`).

### 2. Windows Desktop Control Center (`Desktop/`)
- **Language**: C++20 (Pure WIN32 Subsystem GUI).
- **Glassmorphism Dark UI**: Studio-grade dark theme (`#090A10`), real-time CPU/RAM telemetry, active device cards, live 16:9 viewport with focus reticle, quick action bar, and optics controls.
- **Bilingual Engine**: English <-> Spanish (`[ ESPAÑOL | ENGLISH ]`) instant language switcher.
- **Zero-Copy IPC**: Writes decoded frame buffers to Windows Named Shared Memory (`Global\MiCam_Stream_1`).

### 3. Media Foundation Virtual Camera Driver (`VirtualCamera/`)
- **Technology**: `IMFVirtualCamera` API & DirectShow registration.
- **Multi-Device Support**: Maps each connected iPhone to an independent webcam node (`MiCam Virtual Camera 1`, `MiCam Virtual Camera 2`).

### 4. Native OBS Studio Plugin (`OBSPlugin/`)
- **Technology**: C++ `libobs` SDK (`micam_source`).
- **Direct Pipe**: Ingests frames directly from shared memory for sub-30ms internal latency inside OBS.

---

## 🚀 Remote Build & Deployment Pipeline

- **Remote Build Server**: GitHub Actions (`macos-latest` runner) configured via `ios-builder` CLI.
- **GitHub Repository**: `https://github.com/Samuel-07/IOS-CAM`
- **Output Artifact**: `dist/MiCam-Pro.ipa`
- **Device Deployment**: Signed & installed via MobAI / Sideloadly to device `00008030-00094DDA3AC1802E` (iPhone 11 Pro Max).
