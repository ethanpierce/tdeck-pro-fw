#pragma once
#include <cstdint>
namespace platform {
uint32_t millis();
int gpioRead(int);
void gpioInput(int,bool=false,bool=false);
struct Console {void println(const char*);};
extern Console console;
}
