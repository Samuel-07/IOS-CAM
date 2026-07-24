// Standalone console diagnostic - not part of the shipped product. Prints exactly what
// UsbMuxClient sees so issues can be root-caused instead of guessed at.
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include "UsbMuxClient.h"
#include "MdnsBrowser.h"
#include "../../Shared/protocol.h"

namespace {

void HexDump(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}

bool RecvExactTimed(SOCKET s, void* buffer, int totalBytes, DWORD timeoutMs) {
    DWORD tv = timeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    int received = 0;
    char* p = (char*)buffer;
    while (received < totalBytes) {
        int n = recv(s, p + received, totalBytes - received, 0);
        if (n <= 0) {
            int err = WSAGetLastError();
            std::cout << "  recv() returned " << n << ", WSAGetLastError=" << err
                      << " after " << received << "/" << totalBytes << " bytes\n";
            return false;
        }
        received += n;
    }
    return true;
}

void TestHandshake(SOCKET s) {
    std::cout << "\n=== Sending HandshakeReq, waiting up to 5s for HandshakeResp ===\n";
    MiCamPacketHeader req{};
    req.magic = MICAM_PROTOCOL_MAGIC;
    req.type = static_cast<uint8_t>(MiCamPacketType::HandshakeReq);
    req.payloadSize = 0;
    MiCamHeaderToNetwork(req);
    int sent = send(s, (const char*)&req, sizeof(req), 0);
    std::cout << "  sent " << sent << " bytes (header size=" << sizeof(req) << ")\n";

    MiCamPacketHeader header{};
    if (!RecvExactTimed(s, &header, sizeof(header), 5000)) {
        std::cout << "  NOTHING RECEIVED within 5s - the phone never sent anything back on this socket.\n";
        return;
    }
    MiCamHeaderToHost(header);
    std::cout << "  Got header: magic=0x" << std::hex << header.magic << std::dec
               << " type=" << (int)header.type << " payloadSize=" << header.payloadSize
               << " (expected magic=0x" << std::hex << MICAM_PROTOCOL_MAGIC << std::dec << ")\n";

    if (header.magic != MICAM_PROTOCOL_MAGIC) {
        std::cout << "  MAGIC MISMATCH - this isn't a valid MiCam packet header, wire format is broken.\n";
        return;
    }
    if (header.payloadSize == 0 || header.payloadSize > 4096) {
        std::cout << "  Suspicious payloadSize, not reading further.\n";
        return;
    }

    std::vector<uint8_t> payload(header.payloadSize);
    if (!RecvExactTimed(s, payload.data(), (int)payload.size(), 3000)) {
        std::cout << "  Header arrived but payload did not.\n";
        return;
    }

    std::cout << "  Payload (" << payload.size() << " bytes, expected sizeof(MiCamHandshakeResponse)="
               << sizeof(MiCamHandshakeResponse) << "):\n";
    HexDump(payload.data(), payload.size());

    if (header.type == (uint8_t)MiCamPacketType::HandshakeResp && payload.size() >= sizeof(MiCamHandshakeResponse)) {
        const auto* hs = reinterpret_cast<const MiCamHandshakeResponse*>(payload.data());
        std::cout << "  Parsed OK: uuid=" << std::string(hs->deviceUuid, strnlen(hs->deviceUuid, sizeof(hs->deviceUuid)))
                   << " name=" << std::string(hs->deviceName, strnlen(hs->deviceName, sizeof(hs->deviceName)))
                   << " model=" << std::string(hs->modelName, strnlen(hs->modelName, sizeof(hs->modelName)))
                   << " battery=" << (int)hs->batteryLevel << "\n";
    } else {
        std::cout << "  type/size did not match HandshakeResp - DeviceManager::ReaderLoop would silently ignore this packet.\n";
    }
}

} // namespace

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::cout << "=== UsbMuxClient::ListDevices() ===\n";
    auto devices = UsbMuxClient::ListDevices();
    std::cout << "Found " << devices.size() << " device(s) via usbmuxd\n";
    for (const auto& d : devices) {
        std::cout << "  DeviceID=" << d.deviceId << " UDID=" << d.udid << "\n";
    }

    for (const auto& d : devices) {
        std::cout << "\n=== Connect(deviceId=" << d.deviceId << ", port=50000) ===\n";
        SOCKET s = UsbMuxClient::Connect(d.deviceId, 50000);
        if (s == INVALID_SOCKET) {
            std::cout << "  FAILED (INVALID_SOCKET) - either the app isn't listening on 50000,\n"
                      << "  or usbmuxd's Connect message failed.\n";
        } else {
            std::cout << "  SUCCESS - tunnel open, socket=" << s << "\n";
            TestHandshake(s);
            closesocket(s);
        }
    }

    std::cout << "\n=== MdnsBrowser::Discover(\"_micam._tcp.local.\") ===\n";
    auto services = MdnsBrowser::Discover("_micam._tcp.local.", 1500);
    std::cout << "Found " << services.size() << " WiFi service(s)\n";
    for (const auto& s : services) {
        std::cout << "  name=" << s.name << " ip=" << s.ipAddress << " port=" << s.port
                   << " uuid=" << s.deviceUuid << "\n";
    }

    std::cout << "\nDone. Press Enter to exit.\n";
    std::cin.get();
    WSACleanup();
    return 0;
}
