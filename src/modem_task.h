#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
enum class CellEventKind : uint8_t { Link, Sim, Registration, Signal, Operator, System, Trace, SmsIn, SmsSent, Failed, Call, SmsReady, ApnResult, CarrierStart, Carrier, CarrierDone };
struct CellEvent {
    CellEventKind kind;
    uint32_t id = 0;
    int value = 0; // Failed: 1 means submission result unknown; 0 means definite failure.
    char peer[32] = {};
    char text[512] = {};
};
enum class CellActionKind : uint8_t { Refresh, Sms, Dial, Answer, Hangup, ReadInbox, SetApn, NetworkQuery, CarrierScan, Restart, SelectAutomatic };
struct CellAction {
    CellActionKind kind;
    uint32_t id = 0;
    char peer[24] = {};
    char text[161] = {};
};
extern QueueHandle_t cellEvents, cellActions;
extern std::atomic<uint32_t> droppedCellEvents;
bool startCellTask();
