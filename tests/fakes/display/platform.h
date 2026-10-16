#pragma once
#include <cstddef>
#include <cstdint>
using spi_device_handle_t=void*;
namespace platform {
uint32_t millis();void delay(uint32_t);
void gpioOutput(int,bool);void gpioInput(int,bool=false,bool=false);void gpioWrite(int,bool);int gpioRead(int);
spi_device_handle_t spiDevice(int,uint32_t=4000000,uint8_t=0);
bool spiAcquire(spi_device_handle_t);void spiRelease(spi_device_handle_t);
bool spiTransfer(spi_device_handle_t,const uint8_t*,uint8_t*,size_t);
struct Console {void println(const char*){}};
extern Console console;
}
