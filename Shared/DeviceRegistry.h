#ifndef MICAM_SHARED_DEVICE_REGISTRY_H
#define MICAM_SHARED_DEVICE_REGISTRY_H

// winsock2.h must win the _WINSOCKAPI_ include-guard race against the old winsock.h that
// windows.h pulls in by default - so it has to appear before the first windows.h in this
// translation unit, wherever DeviceRegistry.h happens to get included from.
#include <winsock2.h>
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// Cross-process device list: the Desktop app (MiCamDesktop.exe) is the only thing that
// actually knows which iPhones are connected (USB via usbmuxd, WiFi via mDNS). The OBS
// plugin runs inside a separate obs64.exe process and has no way to reach DeviceManager
// directly, so the Desktop app publishes the live device list here and the OBS plugin
// reads it each time its source-properties dialog is opened, populating the device combo
// with real entries instead of a hardcoded fake list.
#pragma pack(push, 1)
struct DeviceRegistryEntry {
    char uuid[37];             // Stable per-device id (MiCamHandshakeResponse.deviceUuid)
    char displayName[64];      // e.g. "Samuel's iPhone (USB)"
    char streamName[64];       // Shared-memory stream name this device's frames are written to
    char connectionType[16];   // "USB" or "WiFi"
};

struct DeviceRegistryBlock {
    uint32_t magic;            // 'MCDR'
    uint32_t count;
    DeviceRegistryEntry entries[16];
};
#pragma pack(pop)

constexpr uint32_t kDeviceRegistryMagic = 0x4D434452; // "MCDR"
constexpr const char* kDeviceRegistryMapName = "Global\\MiCam_DeviceRegistry";

class DeviceRegistryWriter {
public:
    DeviceRegistryWriter();
    ~DeviceRegistryWriter();

    void Publish(const std::vector<DeviceRegistryEntry>& devices);

private:
    HANDLE m_hMapFile{ NULL };
    LPVOID m_pBuffer{ NULL };
    HANDLE m_hMutex{ NULL };
};

class DeviceRegistryReader {
public:
    DeviceRegistryReader();
    ~DeviceRegistryReader();

    std::vector<DeviceRegistryEntry> Read();

private:
    HANDLE m_hMapFile{ NULL };
    LPVOID m_pBuffer{ NULL };
    HANDLE m_hMutex{ NULL };
};

#endif // MICAM_SHARED_DEVICE_REGISTRY_H
