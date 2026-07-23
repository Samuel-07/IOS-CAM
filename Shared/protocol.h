#ifndef MICAM_SHARED_PROTOCOL_H
#define MICAM_SHARED_PROTOCOL_H

#include <cstdint>

#pragma pack(push, 1)

// Magic bytes for MiCam stream protocol
#define MICAM_PROTOCOL_MAGIC 0x4D43414D // "MCAM"

enum class MiCamPacketType : uint8_t {
    HandshakeReq   = 0x01,
    HandshakeResp  = 0x02,
    VideoConfigCmd = 0x10,
    VideoConfigAck = 0x11,
    VideoFrameData = 0x20,
    AudioFrameData = 0x30,
    TelemetryInfo  = 0x40,
    Ping           = 0xFE,
    Pong           = 0xFF
};

enum class MiCamCodecType : uint8_t {
    H264 = 0x01,
    HEVC = 0x02
};

enum class MiCamCameraLens : uint8_t {
    Wide         = 0x00,
    UltraWide    = 0x01,
    Telephoto    = 0x02,
    Front        = 0x03,
    Macro        = 0x04
};

// Fixed header attached to all network packets (16 bytes)
struct MiCamPacketHeader {
    uint32_t magic;         // MICAM_PROTOCOL_MAGIC (0x4D43414D)
    uint8_t  type;          // MiCamPacketType
    uint8_t  subType;       // Flags / codec
    uint16_t reserved;      // Alignment padding
    uint32_t payloadSize;   // Size of payload following this header
    uint64_t timestampUs;   // Microseconds presentation timestamp
};

// Video frame packet metadata preceding NALUs
struct MiCamVideoFrameHeader {
    uint8_t  codec;         // MiCamCodecType (H264/HEVC)
    uint8_t  isKeyFrame;    // 1 for I-frame/SPS/PPS, 0 for P/B frame
    uint16_t width;         // Frame width
    uint16_t height;        // Frame height
    uint16_t fps;           // Frame rate
    uint16_t reserved;
};

// Camera runtime control command sent from Windows -> iOS
struct MiCamControlCommand {
    uint8_t  lens;               // MiCamCameraLens
    uint16_t targetWidth;        // e.g. 1920, 3840
    uint16_t targetHeight;       // e.g. 1080, 2160
    uint16_t targetFps;          // 24, 30, 60, 120, 240
    uint8_t  torchEnabled;       // 0 or 1
    float    zoomFactor;         // 1.0f to max supported zoom
    float    exposureEV;         // Exposure compensation
    float    focusPosition;      // 0.0 (near) to 1.0 (far), -1.0 for auto
    uint8_t  whiteBalanceMode;   // 0: Auto, 1: Locked, 2: Daylight, etc.
    uint8_t  stabilizationMode;  // 0: Off, 1: Standard, 2: Cinematic
};

// Real-time telemetry sent periodically from iOS -> Windows
struct MiCamTelemetryData {
    uint8_t  batteryLevel;      // 0 to 100 percentage
    uint8_t  isCharging;        // 0 or 1
    uint8_t  thermalState;      // 0: Nominal, 1: Fair, 2: Serious, 3: Critical
    float    currentFps;        // Real-time rendered FPS
    uint32_t currentBitrate;    // Current encoding bitrate (bits/sec)
    uint8_t  connectionType;    // 0: USB (usbmuxd), 1: WiFi
};

#pragma pack(pop)

#endif // MICAM_SHARED_PROTOCOL_H
