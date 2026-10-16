#include "sms_pdu.h"
#include "at_engine.h"
#include <vector>
namespace deck {
namespace {
const char hexChars[] = "0123456789ABCDEF";
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
void utf8(std::string& out, uint16_t c) {
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 63)); }
    else { out += char(0xe0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
}
uint16_t gsm(uint8_t c) {
    static const uint16_t low[32] = {64,163,36,165,232,233,249,236,242,199,10,216,248,13,197,229,
        916,95,934,915,923,937,928,936,931,920,926,0,198,230,223,201};
    if (c < 32) return low[c];
    switch(c) {
    case 36: return 164; case 64: return 161; case 91: return 196; case 92: return 214;
    case 93: return 209; case 94: return 220; case 95: return 167; case 96: return 191;
    case 123: return 228; case 124: return 246; case 125: return 241; case 126: return 252; case 127: return 224;
    default: return c;
    }
}
uint16_t extension(uint8_t c) {
    switch(c) {
    case 10: return 12; case 20: return '^'; case 40: return '{'; case 41: return '}';
    case 47: return '\\'; case 60: return '['; case 61: return '~'; case 62: return ']';
    case 64: return '|'; case 101: return 8364; default: return 0xfffd;
    }
}
}
bool encodeSms(const std::string& number, const std::string& text, SmsSubmit& out) {
    out = {};
    if (!phoneNumber(number) || text.empty()) return false;
    std::vector<uint8_t> septets;
    for (unsigned char c : text) {
        if (c == '@') septets.push_back(0);
        else if (c == '$') septets.push_back(2);
        else if (c == '_') septets.push_back(17);
        else if (c == '\n' || c == '\r') septets.push_back(c);
        else if (c == '^' || c == '{' || c == '}' || c == '\\' || c == '[' || c == '~' || c == ']' || c == '|') {
            septets.push_back(27);
            for (uint8_t code = 0; code < 127; ++code)
                if (extension(code) == c) { septets.push_back(code); break; }
        } else if (c >= 32 && c <= 122 && c != '`') septets.push_back(c);
        else return false;
        if (septets.size() > 160) return false;
    }
    bool international = number[0] == '+';
    std::string digits = international ? number.substr(1) : number;
    std::vector<uint8_t> bytes = {0, 1, 0, uint8_t(digits.size()), uint8_t(international ? 0x91 : 0x81)};
    for (size_t i = 0; i < digits.size(); i += 2)
        bytes.push_back(uint8_t(digits[i] - '0') | uint8_t((i + 1 < digits.size() ? digits[i+1] - '0' : 15) << 4));
    bytes.push_back(0); // PID
    bytes.push_back(0); // GSM-7 DCS
    bytes.push_back(uint8_t(septets.size()));
    size_t base = bytes.size();
    bytes.resize(base + (septets.size() * 7 + 7) / 8, 0);
    for (size_t i = 0; i < septets.size(); ++i) {
        size_t bit = i * 7;
        bytes[base + bit / 8] |= septets[i] << (bit % 8);
        if (bit % 8 > 1) bytes[base + bit / 8 + 1] |= septets[i] >> (8 - bit % 8);
    }
    for (uint8_t b : bytes) { out.hex += hexChars[b >> 4]; out.hex += hexChars[b & 15]; }
    out.tpduBytes = bytes.size() - 1;
    return true;
}
bool decodeSms(const std::string& hex, SmsDeliver& out) {
    out = {};
    if (hex.size() % 2 || hex.size() < 4 || hex.size() > 1024) return false;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.size(); i += 2) {
        int h = nibble(hex[i]), l = nibble(hex[i+1]);
        if (h < 0 || l < 0) return false;
        bytes.push_back(uint8_t((h << 4) | l));
    }
    size_t p = 1 + bytes[0];
    if (p + 3 > bytes.size() || (bytes[p] & 0x43) != 0) return false; // DELIVER, no UDH
    ++p;
    size_t digits = bytes[p++];
    uint8_t ton = bytes[p++];
    if (!digits || digits > 20 || !(ton & 0x80) || (ton & 0x70) == 0x50 || (ton & 0x70) == 0x70 ||
        p + (digits + 1) / 2 + 10 > bytes.size()) return false;
    if (digits % 2 && (bytes[p + digits / 2] & 0xf0) != 0xf0) return false;
    std::string sender = (ton & 0x70) == 0x10 ? "+" : "";
    for (size_t i = 0; i < digits; ++i) {
        uint8_t d = (bytes[p + i/2] >> ((i % 2) * 4)) & 15;
        if (d > 9) return false;
        sender += char('0' + d);
    }
    p += (digits + 1) / 2;
    ++p; // PID
    uint8_t dcs = bytes[p++];
    // General (uncompressed) data coding, with or without message class; GSM-7 class group F.
    // Reject compression, binary alphabets and special message-waiting groups.
    uint8_t alphabet;
    if ((dcs & 0xc0) == 0 && !(dcs & 0x20)) alphabet = dcs & 0x0c;
    else if ((dcs & 0xfc) == 0xf0) alphabet = 0;
    else return false;
    p += 7; // Service centre timestamp, not used as a trusted clock.
    size_t length = bytes[p++];
    std::string text;
    if (alphabet == 0) {
        if (length > 160 || p + (length * 7 + 7) / 8 != bytes.size()) return false;
        bool escaped = false;
        for (size_t i = 0; i < length; ++i) {
            size_t bit = i * 7;
            uint16_t c = bytes[p + bit/8] >> (bit % 8);
            if (bit % 8 > 1) c |= uint16_t(bytes[p + bit/8 + 1]) << (8 - bit % 8);
            c &= 127;
            if (!escaped && c == 27) { escaped = true; continue; }
            utf8(text, escaped ? extension(c) : gsm(c));
            escaped = false;
        }
        if (escaped) return false;
    } else if (alphabet == 8) {
        if (length > 140 || length % 2 || p + length != bytes.size()) return false;
        for (size_t i = 0; i < length; i += 2) {
            uint16_t c = uint16_t(bytes[p+i]) * 256 + bytes[p+i+1];
            if (c >= 0xd800 && c <= 0xdfff) return false;
            utf8(text, c);
        }
    } else return false;
    out = {sender, text};
    return true;
}
uint32_t fingerprint(const std::string& data) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : data) hash = (hash ^ c) * 16777619u;
    return hash;
}
}
