#ifndef MICAM_DESKTOP_DEVICE_MANAGER_H
#define MICAM_DESKTOP_DEVICE_MANAGER_H

#include "../../Shared/protocol.h"
#include "../../Shared/DeviceRegistry.h"
#include "SharedMemoryStream.h"
#include "H264Decoder.h"
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

    // Pulls the most recently decoded RGBA frame for a device, for the Desktop app's own
    // preview panel (the virtual camera and OBS plugin read the same data independently via
    // the per-device SharedMemoryStreamWriter this class also maintains). Returns false if
    // nothing has been decoded yet.
    bool GetLatestFrame(const std::string& deviceId, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight);

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
        std::string        registryKey; // Stable identity used for the shared-memory stream name

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

        // Decode pipeline state - only ever touched from this connection's own ReaderLoop
        // thread, so it doesn't need fieldMutex.
        std::unique_ptr<H264Decoder> decoder;
        uint32_t decoderWidth{ 0 };
        uint32_t decoderHeight{ 0 };
        std::unique_ptr<SharedMemoryStreamWriter> streamWriter;

        // Latest decoded frame, for the Desktop UI's own preview (separate mutex from
        // fieldMutex so painting the UI never blocks on/is blocked by decode work).
        std::mutex frameMutex;
        std::vector<uint8_t> latestRgba;
        uint32_t frameWidth{ 0 };
        uint32_t frameHeight{ 0 };
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
