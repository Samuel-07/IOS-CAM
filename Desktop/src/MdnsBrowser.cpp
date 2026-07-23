#include "MdnsBrowser.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr const char* kMdnsMulticastAddr = "224.0.0.251";
constexpr uint16_t kMdnsPort = 5353;
constexpr uint16_t kDnsTypePTR = 12;
constexpr uint16_t kDnsTypeA = 1;
constexpr uint16_t kDnsTypeTXT = 16;
constexpr uint16_t kDnsTypeSRV = 33;

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

// Decodes a (possibly compressed, RFC 1035 4.1.4) DNS name starting at `offset` within `msg`.
// Advances `offset` to just past the name as it appears in-place in the record (i.e. past a
// compression pointer's 2 bytes, not into the jumped-to location).
std::string ReadName(const uint8_t* msg, size_t len, size_t& offset) {
    std::string name;
    size_t pos = offset;
    bool jumped = false;
    int jumpGuard = 0;

    while (pos < len) {
        uint8_t labelLen = msg[pos];
        if (labelLen == 0) {
            pos += 1;
            if (!jumped) offset = pos;
            break;
        }
        if ((labelLen & 0xC0) == 0xC0) {
            if (pos + 1 >= len) break;
            size_t pointer = ((size_t)(labelLen & 0x3F) << 8) | msg[pos + 1];
            if (!jumped) offset = pos + 2;
            jumped = true;
            if (++jumpGuard > 30 || pointer >= len) break;
            pos = pointer;
            continue;
        }
        pos += 1;
        if (pos + labelLen > len) break;
        if (!name.empty()) name += '.';
        name.append((const char*)msg + pos, labelLen);
        pos += labelLen;
    }
    return name;
}

struct SrvRecord { uint16_t port; std::string target; };

void ParseResponse(const uint8_t* msg, size_t len,
                    std::vector<std::string>& ptrTargets,
                    std::map<std::string, std::string>& txtIdByName,
                    std::map<std::string, SrvRecord>& srvByName,
                    std::map<std::string, std::string>& aByName,
                    const std::string& queryNameLower) {
    if (len < 12) return;

    auto readU16 = [&](size_t o) -> uint16_t { return (uint16_t)((msg[o] << 8) | msg[o + 1]); };
    auto readU32 = [&](size_t o) -> uint32_t {
        return ((uint32_t)msg[o] << 24) | ((uint32_t)msg[o + 1] << 16) | ((uint32_t)msg[o + 2] << 8) | msg[o + 3];
    };

    uint16_t qdcount = readU16(4);
    uint16_t ancount = readU16(6);
    uint16_t nscount = readU16(8);
    uint16_t arcount = readU16(10);

    size_t offset = 12;
    for (uint16_t i = 0; i < qdcount && offset < len; ++i) {
        ReadName(msg, len, offset);
        offset += 4; // QTYPE + QCLASS
    }

    uint32_t totalRecords = (uint32_t)ancount + nscount + arcount;
    for (uint32_t i = 0; i < totalRecords && offset < len; ++i) {
        std::string name = ToLower(ReadName(msg, len, offset));
        if (offset + 10 > len) break;

        uint16_t type = readU16(offset); offset += 2;
        offset += 2; // class (ignore cache-flush bit)
        offset += 4; // ttl
        uint16_t rdlen = readU16(offset); offset += 2;

        size_t rdataStart = offset;
        if (rdataStart + rdlen > len) break;

        if (type == kDnsTypePTR && name == queryNameLower) {
            size_t o = rdataStart;
            ptrTargets.push_back(ReadName(msg, len, o));
        } else if (type == kDnsTypeTXT) {
            size_t p = rdataStart;
            size_t end = rdataStart + rdlen;
            while (p < end) {
                uint8_t segLen = msg[p];
                p += 1;
                if (p + segLen > end) break;
                std::string entry((const char*)msg + p, segLen);
                p += segLen;
                size_t eq = entry.find('=');
                if (eq != std::string::npos && ToLower(entry.substr(0, eq)) == "id") {
                    txtIdByName[name] = entry.substr(eq + 1);
                }
            }
        } else if (type == kDnsTypeSRV && rdlen >= 6) {
            SrvRecord rec{};
            rec.port = readU16(rdataStart + 4);
            size_t o = rdataStart + 6;
            rec.target = ToLower(ReadName(msg, len, o));
            srvByName[name] = rec;
        } else if (type == kDnsTypeA && rdlen == 4) {
            char ipStr[32];
            _snprintf_s(ipStr, sizeof(ipStr), _TRUNCATE, "%u.%u.%u.%u",
                msg[rdataStart], msg[rdataStart + 1], msg[rdataStart + 2], msg[rdataStart + 3]);
            aByName[name] = ipStr;
        }

        offset = rdataStart + rdlen;
    }
}

} // namespace

std::vector<MdnsService> MdnsBrowser::Discover(const std::string& serviceType, uint32_t timeoutMs) {
    std::vector<MdnsService> results;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return results;

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Bind to the mDNS port itself (not an ephemeral one) and join the multicast group. The
    // outgoing query sets the "QU" bit asking for a direct unicast reply, but not every mDNS
    // responder honors that - some always multicast their answer back to 224.0.0.251:5353
    // regardless. Listening only on an ephemeral port would silently miss those replies, which
    // is almost certainly why WiFi discovery was finding 0 services despite the phone actually
    // advertising - this listens for both reply styles.
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(kMdnsPort);
    if (bind(s, (SOCKADDR*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(s);
        return results;
    }

    ip_mreq mreq{};
    inet_pton(AF_INET, kMdnsMulticastAddr, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));

    // Build the DNS query: 1 question, QTYPE=PTR, QCLASS=IN with the top "QU" bit set so
    // compliant mDNS responders (including Apple's on iOS) unicast the reply directly back
    // to us instead of requiring us to join the multicast group to observe it.
    std::vector<uint8_t> query;
    query.resize(12, 0);
    query[5] = 1; // QDCOUNT = 1

    for (size_t start = 0, dot; start <= serviceType.size();) {
        dot = serviceType.find('.', start);
        std::string label = (dot == std::string::npos) ? serviceType.substr(start) : serviceType.substr(start, dot - start);
        if (!label.empty()) {
            query.push_back((uint8_t)label.size());
            for (char c : label) query.push_back((uint8_t)c);
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    query.push_back(0x00); // root label

    query.push_back(0x00); query.push_back((uint8_t)kDnsTypePTR); // QTYPE
    query.push_back(0x80); query.push_back(0x01);                 // QCLASS = IN | QU bit

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kMdnsPort);
    inet_pton(AF_INET, kMdnsMulticastAddr, &dest.sin_addr);

    sendto(s, (const char*)query.data(), (int)query.size(), 0, (SOCKADDR*)&dest, sizeof(dest));

    std::vector<std::string> ptrTargets;
    std::map<std::string, std::string> txtIdByName;
    std::map<std::string, SrvRecord> srvByName;
    std::map<std::string, std::string> aByName;
    std::string queryNameLower = ToLower(serviceType);
    if (!queryNameLower.empty() && queryNameLower.back() != '.') queryNameLower += '.';

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    uint8_t buffer[4096];

    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) break;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s, &readSet);
        timeval tv{ 0, (long)((std::min)((long long)remaining, (long long)300) * 1000) };

        int sel = select(0, &readSet, NULL, NULL, &tv);
        if (sel <= 0) continue;

        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(s, (char*)buffer, sizeof(buffer), 0, (SOCKADDR*)&from, &fromLen);
        if (n <= 0) continue;

        ParseResponse(buffer, (size_t)n, ptrTargets, txtIdByName, srvByName, aByName, queryNameLower);
    }

    closesocket(s);

    for (const auto& instance : ptrTargets) {
        std::string instanceLower = ToLower(instance);
        auto srvIt = srvByName.find(instanceLower);
        if (srvIt == srvByName.end()) continue;

        auto aIt = aByName.find(srvIt->second.target);
        if (aIt == aByName.end()) continue;

        MdnsService svc{};
        // Strip the trailing ".<serviceType>" to recover the human-readable instance name.
        std::string suffix = "." + queryNameLower;
        if (instanceLower.size() > suffix.size() && instanceLower.compare(instanceLower.size() - suffix.size(), suffix.size(), suffix) == 0) {
            svc.name = instance.substr(0, instance.size() - suffix.size());
        } else {
            svc.name = instance;
        }
        svc.ipAddress = aIt->second;
        svc.port = srvIt->second.port;
        auto txtIt = txtIdByName.find(instanceLower);
        svc.deviceUuid = (txtIt != txtIdByName.end()) ? txtIt->second : "";

        results.push_back(svc);
    }

    return results;
}
