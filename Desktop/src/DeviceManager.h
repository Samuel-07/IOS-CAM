#ifndef MICAM_DESKTOP_DEVICE_MANAGER_H
#define MICAM_DESKTOP_DEVICE_MANAGER_H

#include "../../Shared/protocol.h"
#include "../../Shared/DeviceRegistry.h"
#include <winsock2.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

// UI-facing snapshot of a device. Immutable data-only struct handed to the UI callback;
// the live socket/thread state lives in DeviceManager's internal LiveConnection records.
struct ConnectedDevice {
    std::string id;              // Stable key: USB hardware UDID, or app-generated uuid for WiFi-only devices
    std::string uuid;            // App-generated uuid from the handshake (empty until handshake completes)
    std::string name;            // Real device name from handshake ("Samuel's iPhone"), or "Connecting..." before handshake
    std::string model;           // Hardware model identifier from handshake (e.g. "iPhone16,2")
    std::string ipAddress;       // IP or 127.0.0.1 (USB, tunneled via usbmuxd)
    uint16_t    port;
    std::string connectionType;  // "USB (usbmuxd)" or "WiFi (mDNS)"
    uint8_t     batteryLevel;
    uint8_t     isCharging;
    uint8_t     lensMask;        // Bitmask of MiCamLensFlag
    bool        isConnected;
    bool        handshakeDone;
    bool        wifiAvailable;   // A matching mDNS service (same uuid) was seen - "Switch to WiFi" is possible
    std::string wifiIp;
    uint16_t    wifiPort;
};

class DeviceManager {
public:
    using DeviceListCallback = std::function<void(const std::vector<ConnectedDevice>&)>;

    DeviceManager();
    ~DeviceManager();

    void StartDiscovery();
    void StopDiscovery();

    void SetDeviceListCallback(DeviceListCallback callback);

    // Sends a command over the device's already-open persistent connection - never opens a
    // throwaway socket, so it can never knock out an in-progress video stream.
    bool SendControlCommand(const std::string& deviceId, const MiCamControlCommand& cmd);

    // Reconnects the given device over the WiFi endpoint discovered via mDNS (requires
    // wifiAvailable == true on its last published snapshot). Returns false immediately if
    // no WiFi endpoint has been discovered yet for this device.
    bool SwitchToWifi(const std::string& deviceId);

private:
    struct LiveConnection {
        std::mutex        fieldMutex;
        SOCKET             socket{ INVALID_SOCKET };
        std::thread        readerThread;
        std::atomic<bool>  running{ false };
        std::atomic<bool>  connected{ true };
        bool               isUsb{ false };
        int                usbMuxDeviceId{ -1 };
        std::string        ip;
        uint16_t           port{ 0 };

        std::string uuid;
        std::string name{ "Connecting..." };
        std::string model;
        uint8_t     battery{ 0 };
        uint8_t     charging{ 0 };
        uint8_t     lensMask{ 0 };
        bool        handshakeDone{ false };
        bool        wifiAvailable{ false };
        std::string wifiIp;
        uint16_t    wifiPort{ 0 };
    };

    void DiscoveryThreadLoop();
    void ReconcileUsbDevices();
    void ReconcileMdnsDevices();
    void PublishSnapshot();

    std::shared_ptr<LiveConnection> StartConnection(SOCKET sock, bool isUsb, int usbMuxDeviceId, const std::string& ip, uint16_t port);
    void ReaderLoop(std::shared_ptr<LiveConnection> conn);
    void StopConnection(const std::shared_ptr<LiveConnection>& conn);

    std::atomic<bool> m_running{ false };
    std::thread m_discoveryThread;

    std::mutex m_mapMutex;
    // Keyed by USB hardware UDID for USB devices, or app-generated uuid for WiFi-only devices.
    std::vector<std::pair<std::string, std::shared_ptr<LiveConnection>>> m_connections;

    DeviceListCallback m_callback;
    DeviceRegistryWriter m_registryWriter;
};

#endif // MICAM_DESKTOP_DEVICE_MANAGER_H
