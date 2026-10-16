#pragma once
#include <cstdint>
namespace platform {
uint32_t millis();
void gpioOutput(int pin,bool value);
void gpioInput(int pin,bool up=false,bool down=false);
}
