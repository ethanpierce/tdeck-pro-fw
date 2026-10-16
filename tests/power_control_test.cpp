#include <cassert>
#include <functional>
#include <iostream>
#include <vector>
#include "../src/power_control.cpp"
struct Sleeping{};struct TimedOut{};
uint32_t now=0,limit=50000;
bool down=false;
std::function<void()> onTick;
std::vector<int> operations;
platform::Console platform::console;
uint32_t platform::millis(){return now;}
int platform::gpioRead(int pin){assert(pin==board::Boot);return down?0:1;}
void platform::gpioInput(int pin,bool up,bool pullDown){assert(pin==board::Boot && up && !pullDown);}
void platform::Console::println(const char*){}
void vTaskDelay(TickType_t ms){now+=ms;if(onTick)onTick();if(now>=limit)throw TimedOut{};}
void cellSuspendForAirplane(bool value){assert(value);operations.push_back(1);}
void bleSuspendForAirplane(bool value){assert(value);operations.push_back(2);}
void board::holdPowerOff(){operations.push_back(3);}
void gpio_deep_sleep_hold_en(){operations.push_back(5);}
int esp_sleep_disable_wakeup_source(int source){assert(source==ESP_SLEEP_WAKEUP_ALL);operations.push_back(4);return 0;}
void esp_deep_sleep_start(){operations.push_back(6);throw Sleeping{};}
void reset(){requested=false;ready=false;taskHandle=nullptr;now=0;limit=50000;down=false;onTick={};operations.clear();}
void run(){try{fakePowerTask(nullptr);assert(false);}catch(const Sleeping&) {}}
void check(){assert((operations==std::vector<int>{1,2,3,4,5,6}));}
int main(){
    reset();assert(startPowerControl());
    onTick=[](){if(now==100)down=true;if(now==5200){assert(powerOffRequested());powerOffReady();}if(now==6000)down=false;};
    run();assert(now>=6040 && now<6100);check(); // Clean ready waits for BOOT release.
    reset();assert(startPowerControl());
    onTick=[](){if(now==100)down=true;};
    run();assert(powerOffRequested() && !ready.load() && now==30100);check(); // UI never runs.
    reset();assert(startPowerControl());
    onTick=[](){if(now==100)down=true;if(now==5200)powerOffReady();};
    run();assert(now==30100);check(); // Same hold reaches force even after clean cleanup.
    reset();assert(startPowerControl());
    onTick=[](){if(now==100){requestPowerOff();powerOffReady();}};
    run();assert(now==100);check(); // Settings/USB path requires no button hold.
    reset();assert(startPowerControl());down=true;limit=35000;
    try{fakePowerTask(nullptr);assert(false);}catch(const TimedOut&){}
    assert(!powerOffRequested() && operations.empty());
    std::cout<<"PASS actual monitor: 5s cleanup/release, 30s forced shutdown with stalled UI, held-after-clean, Settings path, startup-held BOOT, held rails and RESET-only wake policy\n";
}
