#pragma once
#include <cstdint>
using TickType_t=uint32_t;
constexpr int pdTRUE=1,pdPASS=1;
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(TickType_t);
#ifdef DECK_POWER_CONTROL_TEST
inline void(*fakePowerTask)(void*)=nullptr;
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,int bytes,void*,int priority,void* handle,int core) {
    if(bytes!=4096 || priority!=4 || core!=0)return 0;
    fakePowerTask=fn;*static_cast<void**>(handle)=reinterpret_cast<void*>(1);return pdPASS;
}
#else
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,void*,int) {return pdPASS;}
#endif
