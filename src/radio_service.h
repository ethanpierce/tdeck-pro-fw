#pragma once
#include <RadioLib.h>
#include <radio_packet.h>
#include <functional>
class RadioService {
public:
    RadioService();
    // All methods, including callbacks, run on the main task: the panel shares SPI.
    bool begin();
    bool send(const std::string& text, uint32_t id);
    void poll();
    bool ready=false;
    int error=0;
    uint32_t node=0;
    std::function<void(const deck::RadioPacket&,int)> received;
    std::function<void(uint32_t,bool)> sent;
private:
    SX1262 radio_;
    bool initialized_=false, transmitting_=false;
    uint32_t current_=0, started_=0, sequence_=0, txTimeout_=0, retryAt_=0;
    std::vector<uint8_t> txBytes_;
    void listen(int previousError=RADIOLIB_ERR_NONE);
};
