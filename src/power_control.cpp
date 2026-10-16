// SPDX-License-Identifier: GPL-3.0-only
#include "power_control.h"
#include "board.h"
#include <platform.h>
#include "modem_task.h"
#include "ble_service.h"
#include <power_button.h>
#include <atomic>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace {
std::atomic<bool> requested{false},ready{false};
TaskHandle_t taskHandle=nullptr;
[[noreturn]] void stop(bool forced) {
    // No display, SPI, I2C, storage or UI lock here. This path must work when
    // the UI task is blocked. GPIO holds prevent workers re-enabling the rails.
    cellSuspendForAirplane(true);bleSuspendForAirplane(true);
    board::holdPowerOff();
    if(!forced)platform::console.println("Power off: radios off; RESET starts Handheld OS.");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    gpio_deep_sleep_hold_en();
    // No BOOT/timer/USB wake source: releasing BOOT must not restart the OS.
    // RESET restarts the SoC; this is deep sleep, not battery isolation.
    esp_deep_sleep_start();
    __builtin_unreachable();
}
void monitor(void*) {
    deck::PowerButton button;
    for(;;) {
        const auto event=button.sample(platform::gpioRead(board::Boot)==0,platform::millis());
        if(event==deck::PowerButtonEvent::Force)stop(true);
        if(event==deck::PowerButtonEvent::Shutdown)requested=true;
        // Keep the monitor alive while BOOT is held so the same continuous
        // press can still reach the 30-second force threshold after cleanup.
        if(ready.load() && button.released())stop(false);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}
bool startPowerControl() {
    platform::gpioInput(board::Boot,true);
    return xTaskCreatePinnedToCore(monitor,"power-button",4096,nullptr,4,&taskHandle,0)==pdPASS;
}
void requestPowerOff(){requested=true;}
bool powerOffRequested(){return requested.load();}
void powerOffReady(){ready=true;if(!taskHandle)stop(false);}
size_t powerTaskStackFree(){return taskHandle?uxTaskGetStackHighWaterMark(taskHandle):0;}
