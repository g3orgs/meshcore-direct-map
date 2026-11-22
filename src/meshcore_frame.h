#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace mdm {

struct Frame {
    uint8_t route = 0;
    uint8_t type = 0;
    uint8_t version = 0;
    uint8_t hops = 0;
    uint8_t channel_hash = 0;
    std::string fingerprint;
};

inline int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool decodeHex(std::string_view text, std::vector<uint8_t>& out) {
    out.clear();
    int high = -1;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int nibble = hexValue(c);
        if (nibble < 0) return false;
        if (high < 0) high = nibble;
        else {
            if (out.size() >= 255) return false;
            out.push_back(static_cast<uint8_t>((high << 4) | nibble));
            high = -1;
        }
    }
    return high < 0 && !out.empty();
}

inline bool parseFrame(std::string_view hex, Frame& frame) {
    std::vector<uint8_t> b;
    if (!decodeHex(hex, b) || b.size() < 3) return false;
    const uint8_t route = b[0] & 3;
    const uint8_t type = (b[0] >> 2) & 15;
    const uint8_t version = b[0] >> 6;
    const size_t path_len_offset = (route == 0 || route == 3) ? 5 : 1;
    if (version != 0 || path_len_offset >= b.size()) return false;
    const uint8_t path_len = b[path_len_offset];
    const size_t hash_size = (path_len >> 6) + 1;
    const size_t hops = path_len & 63;
    const size_t path_bytes = hops * hash_size;
    if (hash_size == 4 || path_bytes > 64) return false;
    const size_t payload_offset = path_len_offset + 1 + path_bytes;
    if (payload_offset >= b.size() || b.size() - payload_offset > 184) return false;
    if (type == 5 && b.size() - payload_offset < 4) return false;

    // A stable correlation key across relayed copies. It is not an identity or MAC.
    uint64_t fnv = 14695981039346656037ull;
    auto mix = [&](uint8_t v) { fnv = (fnv ^ v) * 1099511628211ull; };
    mix(type);
    for (size_t i = payload_offset; i < b.size(); ++i) mix(b[i]);
    char key[17];
    std::snprintf(key, sizeof key, "%016llx", static_cast<unsigned long long>(fnv));

    frame.route = route;
    frame.type = type;
    frame.version = version;
    frame.hops = static_cast<uint8_t>(hops);
    frame.channel_hash = b[payload_offset];
    frame.fingerprint = key;
    return true;
}

inline bool isDirectGroup(const Frame& frame, uint8_t channel_hash) {
    return (frame.route == 0 || frame.route == 1) &&
           frame.type == 5 && frame.hops == 0 &&
           frame.channel_hash == channel_hash;
}

} // namespace mdm
