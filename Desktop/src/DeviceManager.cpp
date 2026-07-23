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
    // Check local port forwarded via usbmuxd / Apple Mobile Device Service on 127.0.0.1:50000
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return;

    sockaddr_in clientService{};
    clientService.sin_family = AF_INET;
    clientService.sin_addr.s_addr = inet_addr("127.0.0.1");
    clientService.sin_port = htons(MICAM_PROTOCOL_MAGIC % 65536); // Default 50000

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    connect(s, (SOCKADDR*)&clientService, sizeof(clientService));

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(s, &writeSet);

    timeval tv{ 0, 100000 }; // 100ms
    if (select(0, NULL, &writeSet, NULL, &tv) > 0) {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        
        bool found = false;
        for (auto& dev : m_devices) {
            if (dev.id == "USB_DEVICE_01") {
                dev.isConnected = true;
                found = true;
                break;
            }
        }
        
        if (!found) {
            ConnectedDevice dev;
            dev.id = "USB_DEVICE_01";
            dev.name = "iPhone (USB)";
            dev.model = "Apple iPhone";
            dev.ipAddress = "127.0.0.1";
            dev.port = 50000;
            dev.connectionType = "USB (usbmuxd)";
            dev.batteryLevel = 95;
            dev.isCharging = 1;
            dev.currentWidth = 1920;
            dev.currentHeight = 1080;
            dev.currentFps = 60.0f;
            dev.isConnected = true;
            m_devices.push_back(dev);
        }
    }
    closesocket(s);
}

void DeviceManager::ProbeMdnsDevices() {
    // WiFi mDNS probe logic
}

bool DeviceManager::SendControlCommand(const std::string& deviceId, const MiCamControlCommand& cmd) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(50000);

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
