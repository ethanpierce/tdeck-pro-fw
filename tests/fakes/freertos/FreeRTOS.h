#pragma once
#include <cstdint>
using TickType_t=uint32_t;
constexpr int pdTRUE=1,pdPASS=1;
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(TickType_t);
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,void*,int) {return pdPASS;}
