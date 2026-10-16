#pragma once
#include <freertos/FreeRTOS.h>
using TaskHandle_t=void*;
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 3072;}
