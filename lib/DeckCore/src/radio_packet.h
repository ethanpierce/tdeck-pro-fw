#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace deck {
struct RadioPacket { uint32_t sender = 0, sequence = 0; std::string text; };
bool encodeRadio(const RadioPacket& packet, std::vector<uint8_t>& bytes);
bool decodeRadio(const uint8_t* bytes, size_t size, RadioPacket& packet);
}
