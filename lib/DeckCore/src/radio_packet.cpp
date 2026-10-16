#include "radio_packet.h"
namespace deck {
bool encodeRadio(const RadioPacket& packet, std::vector<uint8_t>& bytes) {
    bytes.clear();
    if (!packet.sender || packet.text.empty() || packet.text.size() > 160) return false;
    for (unsigned char c : packet.text) if (c < 32 || c > 126) return false;
    bytes = {'D', 'K', 1, 0};
    for (uint32_t n : {packet.sender, packet.sequence})
        for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(n >> shift));
    bytes.push_back(uint8_t(packet.text.size()));
    bytes.insert(bytes.end(), packet.text.begin(), packet.text.end());
    return true;
}
bool decodeRadio(const uint8_t* bytes, size_t size, RadioPacket& packet) {
    packet = {};
    if (!bytes || size < 14 || size > 173 || bytes[0] != 'D' || bytes[1] != 'K' || bytes[2] != 1 || bytes[3] != 0 ||
        size != size_t(bytes[12]) + 13) return false;
    uint32_t sender = 0, sequence = 0;
    for (size_t i = 4; i < 8; ++i) sender = (sender << 8) | bytes[i];
    for (size_t i = 8; i < 12; ++i) sequence = (sequence << 8) | bytes[i];
    if (!sender) return false;
    for (size_t i = 13; i < size; ++i) if (bytes[i] < 32 || bytes[i] > 126) return false;
    packet = {sender, sequence, std::string(reinterpret_cast<const char*>(bytes + 13), size - 13)};
    return true;
}
}
