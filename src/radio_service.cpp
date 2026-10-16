#include "radio_service.h"
#include "board.h"
#include "settings.h"
#include <esp_system.h>
RadioService::RadioService(): radio_(new Module(board::RadioCs,board::RadioIrq,board::RadioReset,board::RadioBusy)) {}
bool RadioService::begin() {
    ready=false;
    initialized_=false;
    transmitting_=false;
    if (board::unsupported) return false;
    node=uint32_t(ESP.getEfuseMac() >> 16);
    if (!node) node=1;
    sequence_=esp_random();
    pinMode(board::RadioPower,OUTPUT); digitalWrite(board::RadioPower,HIGH);
    delay(10);
    error=radio_.begin(DECK_LORA_MHZ,DECK_LORA_BANDWIDTH,DECK_LORA_SF,DECK_LORA_CR,
        DECK_LORA_SYNC,DECK_LORA_DBM,8,2.4);
    if (error!=RADIOLIB_ERR_NONE) return false;
    error=radio_.setDio2AsRfSwitch(true);
    if (error!=RADIOLIB_ERR_NONE) return false;
    error=radio_.setCRC(2);
    if (error!=RADIOLIB_ERR_NONE) return false;
    // DIO1 stays asserted until flags are cleared. Poll it on the SPI owner
    // rather than install a GPIO ISR through the framework's small IPC stack.
    initialized_=true;
    listen();
    return ready;
}
void RadioService::listen(int previousError) {
    retryAt_=millis();
    const int state=radio_.startReceive();
    ready=state==RADIOLIB_ERR_NONE;
    // RX rearming must not hide the operation that just failed.
    error=ready?previousError:state;
}
bool RadioService::send(const std::string& text,uint32_t id) {
    if (!ready || transmitting_ || !deck::encodeRadio({node,++sequence_,text},txBytes_)) return false;
    // SX126x packet parameters must be changed in standby, not during RX.
    error=radio_.standby();
    if (error!=RADIOLIB_ERR_NONE) {listen(error); return false;}
    txTimeout_=1000U+2U*((radio_.getTimeOnAir(txBytes_.size())+999U)/1000U);
    error=radio_.startTransmit(txBytes_.data(),txBytes_.size());
    if (error!=RADIOLIB_ERR_NONE) {listen(error); return false;}
    transmitting_=true; current_=id; started_=millis();
    return true;
}
void RadioService::poll() {
    if (!ready) {
        if (initialized_ && uint32_t(millis()-retryAt_)>=1000) listen(error);
        return;
    }
    const bool timedOut=transmitting_ && uint32_t(millis()-started_)>=txTimeout_;
    if (digitalRead(board::RadioIrq)==LOW && !timedOut) return;
    const uint32_t flags=radio_.getIrqFlags();
    if (transmitting_) {
        // finishTransmit() only clears IRQs and enters standby; it does not
        // establish that a packet was transmitted. Check TX_DONE first.
        const bool done=(flags & RADIOLIB_SX126X_IRQ_TX_DONE) && !(flags & RADIOLIB_SX126X_IRQ_TIMEOUT);
        if (!done && !timedOut && !(flags & RADIOLIB_SX126X_IRQ_TIMEOUT)) return;
        int result=done?RADIOLIB_ERR_NONE:RADIOLIB_ERR_TX_TIMEOUT;
        const int finish=radio_.finishTransmit();
        if (result==RADIOLIB_ERR_NONE) result=finish;
        transmitting_=false;
        txBytes_.clear();
        listen(result);
        if (sent) sent(current_,result==RADIOLIB_ERR_NONE);
        return;
    }
    if (!(flags & (RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_TIMEOUT |
                   RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR))) return;

    // Freeze continuous RX before reading its length and buffer so another
    // incoming packet cannot replace them between the two SPI operations.
    int result=radio_.standby();
    uint8_t bytes[255];
    deck::RadioPacket packet;
    bool valid=false;
    int rssi=0;
    if (result==RADIOLIB_ERR_NONE) {
        if (flags & RADIOLIB_SX126X_IRQ_TIMEOUT) result=RADIOLIB_ERR_RX_TIMEOUT;
        else {
            const size_t size=radio_.getPacketLength();
            if (size && size<=sizeof(bytes)) {
                result=radio_.readData(bytes,size);
                valid=result==RADIOLIB_ERR_NONE && deck::decodeRadio(bytes,size,packet) && packet.sender!=node;
                if (valid) rssi=int(radio_.getRSSI());
            } else result=RADIOLIB_ERR_PACKET_TOO_LONG;
        }
    }
    listen(result);
    if (valid && received) received(packet,rssi);
}
