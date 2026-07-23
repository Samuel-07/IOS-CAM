// Standalone console diagnostic - not part of the shipped product. Prints exactly what
// UsbMuxClient sees so a "0 devices" report can be root-caused instead of guessed at.
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include "UsbMuxClient.h"
#include "MdnsBrowser.h"

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
