#include "DeviceManager.h"
#include "UsbMuxClient.h"
#include "MdnsBrowser.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr uint16_t kMiCamPort = 50000;         // NetworkStreamer's fixed listening port on iOS
constexpr const char* kBonjourServiceType = "_micam._tcp.local.";

bool RecvExact(SOCKET s, void* buffer, int totalBytes) {
    int received = 0;
    char* p = (char*)buffer;
    while (received < totalBytes) {
        int n = recv(s, p + received, totalBytes - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool SendExact(SOCKET s, const void* buffer, int totalBytes) {
    int sent = 0;
    const char* p = (const char*)buffer;
    while (sent < totalBytes) {
        int n = send(s, p + sent, totalBytes - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

std::string SafeCString(const char* raw, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && raw[len] != '\0') ++len;
    return std::string(raw, len);
}

SOCKET ConnectTcp(const std::string& ip, uint16_t port, DWORD timeoutMs) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    addr.sin_port = htons(port);

    connect(s, (SOCKADDR*)&addr, sizeof(addr));

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(s, &writeSet);
    timeval tv{ (long)(timeoutMs / 1000), (long)((timeoutMs % 1000) * 1000) };

    if (select(0, NULL, &writeSet, NULL, &tv) <= 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);
    return s;
}

} // namespace

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

    std::lock_guard<std::mutex> lock(m_mapMutex);
    for (auto& kv : m_connections) {
        StopConnection(kv.second);
    }
    m_connections.clear();
}

void DeviceManager::SetDeviceListCallback(DeviceListCallback callback) {
    std::lock_guard<std::mutex> lock(m_mapMutex);
    m_callback = callback;
}

void DeviceManager::DiscoveryThreadLoop() {
    while (m_running) {
        ReconcileUsbDevices();
        ReconcileMdnsDevices();
        PublishSnapshot();

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

void DeviceManager::ReconcileUsbDevices() {
    auto muxDevices = UsbMuxClient::ListDevices();

    std::lock_guard<std::mutex> lock(m_mapMutex);

    // Drop USB entries for devices no longer attached.
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        auto& conn = it->second;
        bool isUsbEntry = conn->isUsb;
        if (isUsbEntry) {
            bool stillAttached = std::any_of(muxDevices.begin(), muxDevices.end(),
                [&](const UsbMuxClient::MuxDevice& d) { return d.deviceId == conn->usbMuxDeviceId; });
            if (!stillAttached) {
                StopConnection(conn);
                it = m_connections.erase(it);
                continue;
            }
        }
        ++it;
    }

    // Connect newly attached devices - and re-home onto USB any device we already know about
    // (by UDID) that's currently riding WiFi, since USB is always lower latency. This is what
    // makes "plug the same iPhone back in after switching it to WiFi" transparently prefer USB
    // again instead of leaving it on WiFi or creating a second, duplicate entry for it.
    for (const auto& mux : muxDevices) {
        std::shared_ptr<LiveConnection> existing;
        for (auto& kv : m_connections) {
            if (kv.first == mux.udid) { existing = kv.second; break; }
        }

        if (existing) {
            bool alreadyOnThisUsbLink = existing->isUsb && existing->usbMuxDeviceId == mux.deviceId;
            if (alreadyOnThisUsbLink) continue;

            SOCKET sock = UsbMuxClient::Connect(mux.deviceId, kMiCamPort);
            if (sock == INVALID_SOCKET) continue;

            SOCKET oldSocket = existing->socket;
            existing->running = false;
            if (oldSocket != INVALID_SOCKET) closesocket(oldSocket);

            {
                std::lock_guard<std::mutex> fieldLock(existing->fieldMutex);
                existing->socket = sock;
                existing->isUsb = true;
                existing->usbMuxDeviceId = mux.deviceId;
                existing->ip = "127.0.0.1";
                existing->port = kMiCamPort;
                existing->handshakeDone = false;
                existing->running = true;
                existing->connected = true;
            }
            std::thread(&DeviceManager::ReaderLoop, this, existing).detach();
            continue;
        }

        SOCKET sock = UsbMuxClient::Connect(mux.deviceId, kMiCamPort);
        if (sock == INVALID_SOCKET) continue; // App likely not running on that device yet

        auto conn = StartConnection(sock, /*isUsb=*/true, mux.deviceId, "127.0.0.1", kMiCamPort);
        m_connections.push_back({ mux.udid, conn });
    }
}

void DeviceManager::ReconcileMdnsDevices() {
    auto services = MdnsBrowser::Discover(kBonjourServiceType, 900);

    std::lock_guard<std::mutex> lock(m_mapMutex);

    for (const auto& svc : services) {
        if (svc.deviceUuid.empty()) continue;

        // Case 1: matches a device we already know about (by app uuid, learned via handshake) -
        // just record that WiFi is available for it, whether it's currently on USB or WiFi.
        bool matchedExisting = false;
        for (auto& kv : m_connections) {
            std::lock_guard<std::mutex> fieldLock(kv.second->fieldMutex);
            if (kv.second->uuid == svc.deviceUuid) {
                kv.second->wifiAvailable = true;
                kv.second->wifiIp = svc.ipAddress;
                kv.second->wifiPort = svc.port;
                matchedExisting = true;
                break;
            }
        }
        if (matchedExisting) continue;

        // Case 2: also check if it's already tracked by uuid as the map key itself
        // (a WiFi-only device connects before any USB handshake exists to match against).
        bool alreadyKeyed = std::any_of(m_connections.begin(), m_connections.end(),
            [&](const auto& kv) { return kv.first == svc.deviceUuid; });
        if (alreadyKeyed) continue;

        // Case 3: brand-new WiFi-only device - connect to it directly.
        SOCKET sock = ConnectTcp(svc.ipAddress, svc.port, 800);
        if (sock == INVALID_SOCKET) continue;

        auto conn = StartConnection(sock, /*isUsb=*/false, -1, svc.ipAddress, svc.port);
        {
            std::lock_guard<std::mutex> fieldLock(conn->fieldMutex);
            conn->uuid = svc.deviceUuid;
        }
        m_connections.push_back({ svc.deviceUuid, conn });
    }
}

std::shared_ptr<DeviceManager::LiveConnection> DeviceManager::StartConnection(
    SOCKET sock, bool isUsb, int usbMuxDeviceId, const std::string& ip, uint16_t port) {
    auto conn = std::make_shared<LiveConnection>();
    conn->socket = sock;
    conn->isUsb = isUsb;
    conn->usbMuxDeviceId = usbMuxDeviceId;
    conn->ip = ip;
    conn->port = port;
    conn->running = true;
    conn->connected = true;

    conn->readerThread = std::thread(&DeviceManager::ReaderLoop, this, conn);
    conn->readerThread.detach();
    return conn;
}

void DeviceManager::ReaderLoop(std::shared_ptr<LiveConnection> conn) {
    // Ask the device to identify itself immediately.
    MiCamPacketHeader req{};
    req.magic = MICAM_PROTOCOL_MAGIC;
    req.type = static_cast<uint8_t>(MiCamPacketType::HandshakeReq);
    req.payloadSize = 0;
    SendExact(conn->socket, &req, sizeof(req));

    while (conn->running) {
        MiCamPacketHeader header{};
        if (!RecvExact(conn->socket, &header, sizeof(header))) break;
        if (header.magic != MICAM_PROTOCOL_MAGIC) break;
        if (header.payloadSize > (16 * 1024 * 1024)) break; // sanity bound

        std::vector<uint8_t> payload(header.payloadSize);
        if (header.payloadSize > 0 && !RecvExact(conn->socket, payload.data(), (int)header.payloadSize)) break;

        auto type = static_cast<MiCamPacketType>(header.type);
        if (type == MiCamPacketType::HandshakeResp && payload.size() >= sizeof(MiCamHandshakeResponse)) {
            const auto* hs = reinterpret_cast<const MiCamHandshakeResponse*>(payload.data());
            std::lock_guard<std::mutex> fieldLock(conn->fieldMutex);
            conn->uuid = SafeCString(hs->deviceUuid, sizeof(hs->deviceUuid));
            conn->name = SafeCString(hs->deviceName, sizeof(hs->deviceName));
            conn->model = SafeCString(hs->modelName, sizeof(hs->modelName));
            conn->battery = hs->batteryLevel;
            conn->charging = hs->isCharging;
            conn->lensMask = hs->availableLensMask;
            conn->handshakeDone = true;
        } else if (type == MiCamPacketType::TelemetryInfo && payload.size() >= sizeof(MiCamTelemetryData)) {
            const auto* tel = reinterpret_cast<const MiCamTelemetryData*>(payload.data());
            std::lock_guard<std::mutex> fieldLock(conn->fieldMutex);
            conn->battery = tel->batteryLevel;
            conn->charging = tel->isCharging;
        }
        // Video frame data isn't decoded on the Windows side yet - intentionally ignored here.
    }

    conn->connected = false;
}

void DeviceManager::StopConnection(const std::shared_ptr<LiveConnection>& conn) {
    conn->running = false;
    if (conn->socket != INVALID_SOCKET) {
        closesocket(conn->socket);
        conn->socket = INVALID_SOCKET;
    }
    // Reader thread is detached and will exit on its own once recv() fails.
}

void DeviceManager::PublishSnapshot() {
    std::vector<ConnectedDevice> snapshot;
    std::vector<DeviceRegistryEntry> registryEntries;
    DeviceListCallback callback;

    {
        std::lock_guard<std::mutex> lock(m_mapMutex);
        callback = m_callback;

        for (auto& kv : m_connections) {
            auto& conn = kv.second;
            std::lock_guard<std::mutex> fieldLock(conn->fieldMutex);

            ConnectedDevice dev{};
            dev.id = kv.first;
            dev.uuid = conn->uuid;
            dev.name = conn->handshakeDone ? conn->name : "Conectando...";
            dev.model = conn->model;
            dev.ipAddress = conn->ip;
            dev.port = conn->port;
            dev.connectionType = conn->isUsb ? "USB (usbmuxd)" : "WiFi (mDNS)";
            dev.batteryLevel = conn->battery;
            dev.isCharging = conn->charging;
            dev.lensMask = conn->lensMask;
            dev.isConnected = conn->connected;
            dev.handshakeDone = conn->handshakeDone;
            dev.wifiAvailable = conn->wifiAvailable;
            dev.wifiIp = conn->wifiIp;
            dev.wifiPort = conn->wifiPort;
            snapshot.push_back(dev);

            if (conn->connected) {
                DeviceRegistryEntry entry{};
                std::string key = !dev.uuid.empty() ? dev.uuid : dev.id;
                strncpy_s(entry.uuid, key.c_str(), sizeof(entry.uuid) - 1);
                std::string label = dev.name + " (" + (conn->isUsb ? "USB" : "WiFi") + ")";
                strncpy_s(entry.displayName, label.c_str(), sizeof(entry.displayName) - 1);
                std::string streamName = "Global\\MiCam_Stream_" + key;
                strncpy_s(entry.streamName, streamName.c_str(), sizeof(entry.streamName) - 1);
                strncpy_s(entry.connectionType, conn->isUsb ? "USB" : "WiFi", sizeof(entry.connectionType) - 1);
                registryEntries.push_back(entry);
            }
        }
    }

    std::sort(snapshot.begin(), snapshot.end(), [](const ConnectedDevice& a, const ConnectedDevice& b) {
        return a.id < b.id;
    });

    m_registryWriter.Publish(registryEntries);

    if (callback) callback(snapshot);
}

bool DeviceManager::SendControlCommand(const std::string& deviceId, const MiCamControlCommand& cmd) {
    std::shared_ptr<LiveConnection> conn;
    {
        std::lock_guard<std::mutex> lock(m_mapMutex);
        for (auto& kv : m_connections) {
            if (kv.first == deviceId) { conn = kv.second; break; }
        }
    }
    if (!conn || conn->socket == INVALID_SOCKET) return false;

    MiCamPacketHeader header{};
    header.magic = MICAM_PROTOCOL_MAGIC;
    header.type = static_cast<uint8_t>(MiCamPacketType::VideoConfigCmd);
    header.payloadSize = sizeof(MiCamControlCommand);

    // Reuses the SAME persistent socket the device is already streaming/listening on -
    // never opens a throwaway connection (that used to make the iPhone tear down its
    // active NWConnection every time a lens/torch/zoom command was sent).
    if (!SendExact(conn->socket, &header, sizeof(header))) return false;
    if (!SendExact(conn->socket, &cmd, sizeof(cmd))) return false;
    return true;
}

bool DeviceManager::SwitchToWifi(const std::string& deviceId) {
    std::shared_ptr<LiveConnection> conn;
    std::string wifiIp;
    uint16_t wifiPort = 0;

    {
        std::lock_guard<std::mutex> lock(m_mapMutex);
        for (auto& kv : m_connections) {
            if (kv.first == deviceId) {
                conn = kv.second;
                std::lock_guard<std::mutex> fieldLock(conn->fieldMutex);
                if (!conn->wifiAvailable) return false;
                wifiIp = conn->wifiIp;
                wifiPort = conn->wifiPort;
                break;
            }
        }
    }
    if (!conn || wifiIp.empty()) return false;

    SOCKET newSocket = ConnectTcp(wifiIp, wifiPort, 1500);
    if (newSocket == INVALID_SOCKET) return false;

    // Tear down the old (USB) socket - its reader thread will exit on its own - and start
    // a fresh reader on the new WiFi socket, reusing the same LiveConnection/map key so the
    // UI slot doesn't duplicate or flicker.
    SOCKET oldSocket = conn->socket;
    conn->running = false;
    if (oldSocket != INVALID_SOCKET) closesocket(oldSocket);

    {
        std::lock_guard<std::mutex> fieldLock(conn->fieldMutex);
        conn->socket = newSocket;
        conn->isUsb = false;
        conn->usbMuxDeviceId = -1;
        conn->ip = wifiIp;
        conn->port = wifiPort;
        conn->handshakeDone = false;
        conn->running = true;
        conn->connected = true;
    }

    std::thread(&DeviceManager::ReaderLoop, this, conn).detach();
    return true;
}
