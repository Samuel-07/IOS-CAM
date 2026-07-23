#ifndef MICAM_DESKTOP_DEVICE_MANAGER_H
#define MICAM_DESKTOP_DEVICE_MANAGER_H

#include "../../Shared/protocol.h"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

struct ConnectedDevice {
    std::string id;             // Unique Device ID or UDID
    std::string name;           // Friendly device name (e.g. "Samuel's iPhone")
    std::string model;          // e.g. "iPhone 15 Pro"
    std::string ipAddress;      // IP or 127.0.0.1 for USB
    uint16_t    port;           // Port (default 50000)
    std::string connectionType; // "USB (usbmuxd)" or "WiFi (mDNS)"
    uint8_t     batteryLevel;
    uint8_t     isCharging;
    uint32_t    currentWidth;
    uint32_t    currentHeight;
    float       currentFps;
    bool        isConnected;
};

class DeviceManager {
public:
    using DeviceListCallback = std::function<void(const std::vector<ConnectedDevice>&)>;

    DeviceManager();
    ~DeviceManager();

    void StartDiscovery();
    void StopDiscovery();

    void SetDeviceListCallback(DeviceListCallback callback);

    // Send control command to a connected iPhone
    bool SendControlCommand(const std::string& deviceId, const MiCamControlCommand& cmd);

private:
    void DiscoveryThreadLoop();
    void ProbeUsbDevices();
    void ProbeMdnsDevices();

    std::atomic<bool> m_running{false};
    std::thread m_discoveryThread;
    std::mutex m_deviceMutex;
    std::vector<ConnectedDevice> m_devices;
    DeviceListCallback m_callback;
};

#endif // MICAM_DESKTOP_DEVICE_MANAGER_H
