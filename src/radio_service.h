#pragma once
#include <RadioLib.h>
#include <radio_packet.h>
#include <mesh_protocol.h>
#include <radio_gate.h>
#include <functional>
#include <string>
class RadioService {
public:
    RadioService();
    // Main-task owner only: the panel shares SPI, including every callback.
    bool begin();
    bool configure(const deck::MeshOptions& options);
    const deck::MeshOptions& options()const{return options_;}
    const std::string& status()const{return status_;}
    const std::array<deck::MeshNode,16>& nodes()const{return nodes_;}
    bool enabled()const{return gate_.preferred();}
    bool configured()const{return deck::validMeshOptions(options_) && options_.region==deck::MeshRegion::US;}
    bool cryptoReady()const{return cryptoReady_;}
    void suspendForAirplane(bool suspended);
    bool send(const std::string& text,uint32_t id);
    bool announce(); // Explicit NODEINFO broadcast; sent callback id=0.
    void poll();
    bool ready=false;
    int error=0;
    uint32_t node=0;
    std::function<void(const deck::RadioPacket&,int)> received;
    std::function<void(uint32_t,bool)> sent;
    std::function<void()> changed;
private:
    SX1262 radio_;
    deck::MeshOptions options_{};deck::RadioGate gate_;
    deck::MeshDuplicateCache duplicates_;
    std::array<deck::MeshNode,16> nodes_{};
    deck::MeshBuffer txBytes_{};
    bool initialized_=false,transmitting_=false,queued_=false,cad_=false,cryptoReady_=false;
    uint8_t cadAttempts_=0;
    uint32_t current_=0,started_=0,sequence_=0,txTimeout_=0,retryAt_=0,sendAt_=0,queuedAt_=0;
    std::string status_="Mesh off";
    void report(const char* text);
    void powerDown();
    bool queueData(uint32_t port,const uint8_t* bytes,size_t size,uint32_t id);
    void complete(bool success,int result);
    void receivePacket(const uint8_t* bytes,size_t size,int rssi);
    void heard(uint32_t from,int rssi,const deck::MeshUser* user=nullptr);
    void listen(int previousError=RADIOLIB_ERR_NONE);
};
