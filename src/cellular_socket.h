#pragma once
#include <platform.h>
#include <at_engine.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <functional>
#include <string>

// Blocking API for the AI worker only. All UART access remains in CellService.
// The modem transports ciphertext; TLS and bearer headers stay on the ESP32.
bool cellTcpReady();
void cellTcpSuspendForAirplane(bool suspended);
bool cellTcpOpen(const std::string& host,uint16_t port,uint32_t timeout=35000);
int cellTcpWrite(const unsigned char* data,size_t size,uint32_t timeout=10000);
// >0 bytes, 0 no data yet, -1 failure, -2 remote EOF.
int cellTcpRead(unsigned char* data,size_t size,uint32_t timeout=10000);
void cellTcpClose();
int64_t cellNetworkTime();
#ifdef DECK_CELLULAR_SOCKET_TEST
// Opaque, asynchronous host-test seam; never present in device builds.
enum class CellTcpTestKind {Open,Write,Read,Close,Clock};
void* cellTcpTestSubmit(CellTcpTestKind kind,const std::string& data="",uint16_t port=443,uint32_t timeout=35000);
bool cellTcpTestTake(void* request,int& result,std::string& data,int64_t& timestamp);
#endif

struct CellTcpRequest;
class CellularSocketBroker {
public:
    bool begin();
    // Sanitized stage/category/numeric-result only; never UART or TLS data.
    std::function<void(const std::string&)> diagnostic;
    // Called only by the UART owner. ready permits a new operation; an active
    // operation completes regardless of a later readiness change.
    void poll(deck::AtEngine& engine,bool ready,uint32_t now);
    void onUrc(const std::string& line);
    void cancel();
    bool busy()const{return active_!=nullptr;}
    bool activeConnection()const{return opened_ || active_!=nullptr;}
private:
    CellTcpRequest* active_=nullptr;
    deck::AtEngine* engine_=nullptr;
    uint32_t started_=0,generation_=0;
    bool ack_=false,received_=false,remoteClosed_=false,opened_=false,poisoned_=false;
    int stage_=0,asyncResult_=-1;
    const char* failureCategory_="protocol error";
    void command(const std::string& text,int next,const std::string& payload="");
    void completed(const deck::AtResult& result,int next,uint32_t generation);
    void advance();
    void finish(int result,const std::string& data="",int64_t timestamp=0);
};
