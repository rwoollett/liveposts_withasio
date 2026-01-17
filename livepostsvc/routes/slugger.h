#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <iomanip>

namespace slugger {

// ------------------------------------------------------------
// Minimal header-only xxHash32 implementation
// ------------------------------------------------------------
inline uint32_t xxhash32(const void* input, size_t len, uint32_t seed = 0) {
    const uint32_t PRIME32_1 = 0x9E3779B1U;
    const uint32_t PRIME32_2 = 0x85EBCA77U;
    const uint32_t PRIME32_3 = 0xC2B2AE3DU;
    const uint32_t PRIME32_4 = 0x27D4EB2FU;
    const uint32_t PRIME32_5 = 0x165667B1U;

    const uint8_t* p = (const uint8_t*)input;
    const uint8_t* const end = p + len;

    uint32_t h32;

    if (len >= 16) {
        const uint8_t* const limit = end - 16;
        uint32_t v1 = seed + PRIME32_1 + PRIME32_2;
        uint32_t v2 = seed + PRIME32_2;
        uint32_t v3 = seed + 0;
        uint32_t v4 = seed - PRIME32_1;

        do {
            v1 += *(uint32_t*)p * PRIME32_2; p += 4; v1 = (v1 << 13) | (v1 >> 19); v1 *= PRIME32_1;
            v2 += *(uint32_t*)p * PRIME32_2; p += 4; v2 = (v2 << 13) | (v2 >> 19); v2 *= PRIME32_1;
            v3 += *(uint32_t*)p * PRIME32_2; p += 4; v3 = (v3 << 13) | (v3 >> 19); v3 *= PRIME32_1;
            v4 += *(uint32_t*)p * PRIME32_2; p += 4; v4 = (v4 << 13) | (v4 >> 19); v4 *= PRIME32_1;
        } while (p <= limit);

        h32 = ((v1 << 1) | (v1 >> 31)) +
              ((v2 << 7) | (v2 >> 25)) +
              ((v3 << 12) | (v3 >> 20)) +
              ((v4 << 18) | (v4 >> 14));
    } else {
        h32 = seed + PRIME32_5;
    }

    h32 += (uint32_t)len;

    while (p + 4 <= end) {
        h32 += *(uint32_t*)p * PRIME32_3;
        h32 = ((h32 << 17) | (h32 >> 15)) * PRIME32_4;
        p += 4;
    }

    while (p < end) {
        h32 += (*p) * PRIME32_5;
        h32 = ((h32 << 11) | (h32 >> 21)) * PRIME32_1;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}

// ------------------------------------------------------------
// ASCII normalize: lowercase, strip punctuation, basic accent removal
// ------------------------------------------------------------
inline std::string ascii_normalize(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (unsigned char c : input) {
        if (c >= 192 && c <= 255) {
            static const char* map =
                "AAAAAAACEEEEIIII"
                "DNOOOOOxOUUUUYTs"
                "aaaaaaaceeeeiiii"
                "dnooooo/ouuuuyty";
            out.push_back(map[c - 192]);
            continue;
        }

        if (std::isalnum(c) || std::isspace(c) || c == '-') {
            out.push_back(std::tolower(c));
        }
    }

    return out;
}

// ------------------------------------------------------------
// Replace whitespace → '-' and collapse multiple dashes
// ------------------------------------------------------------
inline std::string slugify_basic(const std::string& input) {
    std::string s = input;

    std::replace_if(s.begin(), s.end(),
        [](char c){ return std::isspace(static_cast<unsigned char>(c)); },
        '-');

    s = std::regex_replace(s, std::regex("-+"), "-");

    if (!s.empty() && s.front() == '-') s.erase(0, 1);
    if (!s.empty() && s.back() == '-') s.pop_back();

    return s;
}

// ------------------------------------------------------------
// Smart trim: avoid cutting mid-word if possible
// ------------------------------------------------------------
inline std::string smart_trim(const std::string& slug, size_t maxLen) {
    if (slug.size() <= maxLen) return slug;

    size_t cut = slug.rfind('-', maxLen);
    if (cut != std::string::npos && cut > 0) {
        return slug.substr(0, cut);
    }

    std::string out = slug.substr(0, maxLen);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// ------------------------------------------------------------
// Convert xxHash32 → 6-char hex string
// ------------------------------------------------------------
inline std::string short_hash(const std::string& key) {
    uint32_t h = xxhash32(key.data(), key.size(), 0x12345678U);

    std::ostringstream oss;
    oss << std::hex << std::setw(6) << std::setfill('0') << (h & 0xFFFFFF);
    return oss.str();
}

// ------------------------------------------------------------
// Public API: deterministic, mid-word-safe slug
// ------------------------------------------------------------
inline std::string make_slug(
    const std::string& title,
    const std::string& uniqueKey,
    size_t maxLen = 30
) {
    std::string norm = ascii_normalize(title);
    std::string base = slugify_basic(norm);
    base = smart_trim(base, maxLen);

    std::string hash = short_hash(uniqueKey);
    return base + "-" + hash;
}

} // namespace slugger
