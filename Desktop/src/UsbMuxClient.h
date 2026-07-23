#ifndef MICAM_DESKTOP_USBMUX_CLIENT_H
#define MICAM_DESKTOP_USBMUX_CLIENT_H

#include <winsock2.h>
#include <string>
#include <vector>
#include <cstdint>

// Minimal client for the usbmuxd wire protocol used by "Apple Mobile Device Support"
// (installed alongside iTunes / the Apple Devices app). On Windows this service listens
// on a TCP loopback socket (127.0.0.1:27015) and multiplexes TCP connections to any port
// a USB-attached iPhone is listening on, without requiring per-app entitlements.
//
// This is what makes REAL simultaneous multi-iPhone USB detection possible: each attached
// device gets its own usbmuxd "DeviceID", and Connect() opens an independent tunnel to that
// specific device's TCP port (our iOS app's NetworkStreamer listener), so two iPhones plugged
// in at once get two distinct sockets instead of both racing for the same loopback port.
class UsbMuxClient {
public:
    struct MuxDevice {
        int         deviceId;   // usbmuxd's internal id for this attachment, valid until unplugged
        std::string udid;       // Apple hardware UDID (SerialNumber), stable per physical device
    };

    // Returns all iPhones currently visible to usbmuxd over USB. Returns an empty vector
    // (not an error) if Apple Mobile Device Support isn't installed/running.
    static std::vector<MuxDevice> ListDevices();

    // Opens a raw TCP tunnel to `port` on the given device via usbmuxd's Connect message.
    // On success the returned socket is a plain, un-framed duplex stream directly to that
    // port on the device (same wire format as a WiFi connection to the same port) -
    // caller owns the socket and must closesocket() it. Returns INVALID_SOCKET on failure.
    static SOCKET Connect(int deviceId, uint16_t port);

private:
    static SOCKET ConnectToMuxd();
    static bool SendPlistMessage(SOCKET s, const std::string& plistBody, uint32_t tag);
    static bool ReadPlistMessage(SOCKET s, std::string& outBody, uint32_t timeoutMs);
};

#endif // MICAM_DESKTOP_USBMUX_CLIENT_H
