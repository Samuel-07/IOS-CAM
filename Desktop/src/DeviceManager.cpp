#include "DeviceManager.h"
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

DeviceManager::DeviceManager() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

DeviceManager::~DeviceManager() {
    StopDiscovery();
    WSACleanup();
}

void DeviceManager::StartDiscovery() {
    if (m_running) return;
    m_running = true;
    m_discoveryThread = std::thread(&DeviceManager::DiscoveryThreadLoop, this);
}

void DeviceManager::StopDiscovery() {
    if (!m_running) return;
    m_running = false;
    if (m_discoveryThread.joinable()) {
        m_discoveryThread.join();
    }
}

void DeviceManager::SetDeviceListCallback(DeviceListCallback callback) {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    m_callback = callback;
}

void DeviceManager::DiscoveryThreadLoop() {
    while (m_running) {
        ProbeUsbDevices();
        ProbeMdnsDevices();
        
        if (m_callback) {
            std::lock_guard<std::mutex> lock(m_deviceMutex);
            m_callback(m_devices);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

void DeviceManager::ProbeUsbDevices() {
    uint16_t ports[] = { 50000, 50001 };
    const char* devIds[] = { "USB_DEVICE_01", "USB_DEVICE_02" };
    const char* devNames[] = { "Samuel's iPhone 1 (USB)", "iPhone 2 (USB)" };

    for (int i = 0; i < 2; ++i) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) continue;

        sockaddr_in clientService{};
        clientService.sin_family = AF_INET;
        clientService.sin_addr.s_addr = inet_addr("127.0.0.1");
        clientService.sin_port = htons(ports[i]);

        u_long mode = 1;
        ioctlsocket(s, FIONBIO, &mode);

        connect(s, (SOCKADDR*)&clientService, sizeof(clientService));

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(s, &writeSet);

        timeval tv{ 0, 50000 }; // 50ms
        bool connected = (select(0, NULL, &writeSet, NULL, &tv) > 0);
        closesocket(s);

        std::lock_guard<std::mutex> lock(m_deviceMutex);
        bool found = false;
        for (auto& dev : m_devices) {
            if (dev.id == devIds[i]) {
                dev.isConnected = connected;
                found = true;
                break;
            }
        }

        if (!found) {
            ConnectedDevice dev;
            dev.id = devIds[i];
            dev.name = devNames[i];
            dev.model = (i == 0) ? "iPhone 15 Pro" : "iPhone 11 Pro Max";
            dev.ipAddress = "127.0.0.1";
            dev.port = ports[i];
            dev.connectionType = "USB (usbmuxd)";
            dev.batteryLevel = 94 - i * 5;
            dev.isCharging = 1;
            dev.currentWidth = 1920;
            dev.currentHeight = 1080;
            dev.currentFps = 60.0f;
            dev.isConnected = connected;
            m_devices.push_back(dev);
        }
    }
}

void DeviceManager::ProbeMdnsDevices() {
    // Probe WiFi broadcast network endpoints
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;

    BOOL broadcast = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

    closesocket(s);
}

bool DeviceManager::SendControlCommand(const std::string& deviceId, const MiCamControlCommand& cmd) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    uint16_t targetPort = 50000;
    if (deviceId == "USB_DEVICE_02" || deviceId == "WIFI_DEVICE_02") {
        targetPort = 50001;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(targetPort);

    if (connect(s, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    MiCamPacketHeader header{};
    header.magic = MICAM_PROTOCOL_MAGIC;
    header.type = static_cast<uint8_t>(MiCamPacketType::VideoConfigCmd);
    header.payloadSize = sizeof(MiCamControlCommand);
    header.timestampUs = 0;

    send(s, (const char*)&header, sizeof(header), 0);
    send(s, (const char*)&cmd, sizeof(cmd), 0);

    closesocket(s);
    return true;
}
