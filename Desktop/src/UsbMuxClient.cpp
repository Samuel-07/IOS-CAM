#include "UsbMuxClient.h"
#include <ws2tcpip.h>
#include <regex>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr uint16_t kUsbmuxdPort = 27015; // Apple Mobile Device Support's TCP relay on Windows
constexpr uint32_t kProtoVersionPlist = 1;
constexpr uint32_t kMsgTypePlist = 8;

bool RecvExact(SOCKET s, char* buffer, int totalBytes, uint32_t timeoutMs) {
    DWORD tv = timeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    int received = 0;
    while (received < totalBytes) {
        int n = recv(s, buffer + received, totalBytes - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool SendExact(SOCKET s, const char* data, int totalBytes) {
    int sent = 0;
    while (sent < totalBytes) {
        int n = send(s, data + sent, totalBytes - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

} // namespace

SOCKET UsbMuxClient::ConnectToMuxd() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(kUsbmuxdPort);

    DWORD connectTimeout = 500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&connectTimeout, sizeof(connectTimeout));

    if (connect(s, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

bool UsbMuxClient::SendPlistMessage(SOCKET s, const std::string& plistBody, uint32_t tag) {
    struct Header {
        uint32_t length;
        uint32_t version;
        uint32_t message;
        uint32_t tag;
    } header{};

    header.length = static_cast<uint32_t>(sizeof(Header) + plistBody.size());
    header.version = kProtoVersionPlist;
    header.message = kMsgTypePlist;
    header.tag = tag;

    std::string packet;
    packet.resize(sizeof(Header));
    memcpy(&packet[0], &header, sizeof(Header));
    packet += plistBody;

    return SendExact(s, packet.data(), static_cast<int>(packet.size()));
}

bool UsbMuxClient::ReadPlistMessage(SOCKET s, std::string& outBody, uint32_t timeoutMs) {
    struct Header {
        uint32_t length;
        uint32_t version;
        uint32_t message;
        uint32_t tag;
    } header{};

    if (!RecvExact(s, (char*)&header, sizeof(Header), timeoutMs)) return false;
    if (header.length < sizeof(Header) || header.length > (16 * 1024 * 1024)) return false;

    uint32_t bodySize = header.length - sizeof(Header);
    outBody.resize(bodySize);
    if (bodySize == 0) return true;
    return RecvExact(s, &outBody[0], static_cast<int>(bodySize), timeoutMs);
}

std::vector<UsbMuxClient::MuxDevice> UsbMuxClient::ListDevices() {
    std::vector<MuxDevice> result;

    SOCKET s = ConnectToMuxd();
    if (s == INVALID_SOCKET) return result; // Apple Mobile Device Support not installed/running

    static const char* kListDevicesRequest =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>ClientVersionString</key><string>MiCamStudioPro</string>"
        "<key>MessageType</key><string>ListDevices</string>"
        "<key>ProgName</key><string>MiCamStudioPro</string>"
        "<key>kLibUSBMuxVersion</key><integer>3</integer>"
        "</dict></plist>";

    if (!SendPlistMessage(s, kListDevicesRequest, 1)) {
        closesocket(s);
        return result;
    }

    std::string response;
    if (!ReadPlistMessage(s, response, 800)) {
        closesocket(s);
        return result;
    }
    closesocket(s);

    // Pragmatic extraction instead of a full plist/XML parser: usbmuxd's ListDevices reply
    // emits one <key>DeviceID</key><integer>N</integer> and one nested
    // <key>SerialNumber</key><string>UDID</string> per attached device, both in a fixed,
    // consistent order. Zip them positionally rather than parsing the plist tree.
    std::regex deviceIdRe("<key>DeviceID</key>\\s*<integer>(\\d+)</integer>");
    std::regex serialRe("<key>SerialNumber</key>\\s*<string>([^<]+)</string>");

    std::vector<int> ids;
    for (auto it = std::sregex_iterator(response.begin(), response.end(), deviceIdRe); it != std::sregex_iterator(); ++it) {
        ids.push_back(std::stoi((*it)[1].str()));
    }

    std::vector<std::string> serials;
    for (auto it = std::sregex_iterator(response.begin(), response.end(), serialRe); it != std::sregex_iterator(); ++it) {
        serials.push_back((*it)[1].str());
    }

    size_t count = (std::min)(ids.size(), serials.size());
    for (size_t i = 0; i < count; ++i) {
        result.push_back(MuxDevice{ ids[i], serials[i] });
    }

    return result;
}

SOCKET UsbMuxClient::Connect(int deviceId, uint16_t port) {
    SOCKET s = ConnectToMuxd();
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    // usbmuxd expects the port already in network byte order, carried as a plain integer.
    uint16_t netPort = htons(port);

    char body[512];
    _snprintf_s(body, sizeof(body), _TRUNCATE,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<plist version=\"1.0\"><dict>"
        "<key>ClientVersionString</key><string>MiCamStudioPro</string>"
        "<key>MessageType</key><string>Connect</string>"
        "<key>ProgName</key><string>MiCamStudioPro</string>"
        "<key>DeviceID</key><integer>%d</integer>"
        "<key>PortNumber</key><integer>%u</integer>"
        "</dict></plist>",
        deviceId, (unsigned)netPort);

    if (!SendPlistMessage(s, body, 2)) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    // Tunneling a Connect over real USB (vs. a local ListDevices query) can take noticeably
    // longer than a plain metadata round-trip, so this gets a longer timeout than ListDevices.
    std::string response;
    if (!ReadPlistMessage(s, response, 3000)) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    // Success looks like "<key>Number</key><integer>0</integer>", but real plist XML is
    // pretty-printed with whitespace/newlines between tags - a literal substring match here
    // was reporting successful connects as failures. Match the same whitespace-tolerant way
    // ListDevices() already does.
    static const std::regex successRe("<key>Number</key>\\s*<integer>0</integer>");
    if (!std::regex_search(response, successRe)) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    // From here on `s` is a raw, un-framed tunnel straight to the device's TCP port.
    DWORD clearTimeout = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&clearTimeout, sizeof(clearTimeout));
    return s;
}
