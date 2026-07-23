#ifndef MICAM_DESKTOP_MDNS_BROWSER_H
#define MICAM_DESKTOP_MDNS_BROWSER_H

#include <string>
#include <vector>
#include <cstdint>

// A discovered iPhone advertising the MiCam NetworkStreamer service over WiFi via Bonjour.
struct MdnsService {
    std::string name;       // Bonjour instance name (device's UIDevice.current.name)
    std::string ipAddress;  // Resolved IPv4 address on the LAN
    uint16_t    port;       // TCP port the NetworkStreamer listener is bound to
    std::string deviceUuid; // From the "id" TXT record - matches MiCamHandshakeResponse.deviceUuid,
                             // used to correlate this WiFi service with a USB-connected device.
};

// Minimal DNS-SD / mDNS browser implemented from scratch against raw UDP multicast so WiFi
// discovery works out of the box without depending on Apple's Bonjour service being installed
// on Windows (it usually isn't, unless iTunes/an Apple app installed it).
class MdnsBrowser {
public:
    // Sends a one-shot PTR query for `serviceType` (e.g. "_micam._tcp.local.") to the mDNS
    // multicast group (224.0.0.251:5353) and collects responses for `timeoutMs`.
    static std::vector<MdnsService> Discover(const std::string& serviceType, uint32_t timeoutMs = 1200);
};

#endif // MICAM_DESKTOP_MDNS_BROWSER_H
