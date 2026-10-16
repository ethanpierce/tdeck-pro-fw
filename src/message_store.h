#pragma once
#include <Preferences.h>
#include <vector>
#include <cstdint>
enum class Medium : uint8_t { Sms, Radio };
enum class Delivery : uint8_t { Pending, Submitted, Received, Failed, Unknown };
struct Message {
    uint32_t magic=0x444b0001, sequence=0, reference=0, checksum=0;
    Medium medium=Medium::Sms;
    Delivery delivery=Delivery::Pending;
    uint8_t incoming=0, reserved=0;
    char peer[32]={}, text[481]={};
};
class MessageStore {
public:
    void begin();
    uint32_t add(Medium medium, bool incoming, const char* peer, const char* text, uint32_t reference=0);
    void update(uint32_t sequence,Delivery delivery);
    const std::vector<Message>& messages() const { return messages_; }
    bool persistent=false;
private:
    bool save(Message& message);
    Preferences prefs_;
    std::vector<Message> messages_;
    uint32_t next_=1;
};
