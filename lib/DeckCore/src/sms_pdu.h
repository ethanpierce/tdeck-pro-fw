#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace deck {
struct SmsSubmit { std::string hex; size_t tpduBytes = 0; };
struct SmsDeliver { std::string sender, text; };
// One SMS, GSM-7 ASCII subset plus extension characters. No silent truncation.
bool encodeSms(const std::string& number, const std::string& text, SmsSubmit& out);
// GSM-7 or UCS-2 SMS-DELIVER. Rejects UDH/concatenated and alphanumeric senders.
bool decodeSms(const std::string& hex, SmsDeliver& out);
uint32_t fingerprint(const std::string& data);
}
