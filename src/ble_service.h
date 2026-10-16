#pragma once
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
enum class BleCommand : uint8_t {Enable,Disable,Scan,Advertise,Connect,Disconnect,ForgetKeyboard,ForgetAll,Confirm,Passkey,CancelPair,Status};
struct BleRequest {
    BleCommand kind;
    uint32_t ticket=0;
    uint32_t value=0;
    uint16_t handle=0;
    uint8_t addressType=0,battery=0;
    char address[18]={},text[160]={};
};
enum class BleEventKind : uint8_t {State,Notice,ScanStart,Device,ScanDone,Phone,Keyboard,RememberKeyboard,Layout,Hid,PairConfirm,PairEntry,PairDisplay,PairDone,Advertising};
struct BleEvent {
    BleEventKind kind;
    uint32_t value=0;
    uint16_t handle=0;
    uint8_t addressType=0,length=0;
    char address[18]={},text[80]={};
    uint8_t bytes[32]={};
};
extern QueueHandle_t bleRequests,bleEvents;
extern std::atomic<uint32_t> droppedBleEvents;
bool startBleTask();
void bleSuspendForAirplane(bool suspended);
bool bleAirplaneStopped();
bool bleRequest(BleCommand kind,const char* address="",uint8_t addressType=0,uint32_t value=0,uint16_t handle=0,const char* text="",uint8_t battery=0);
