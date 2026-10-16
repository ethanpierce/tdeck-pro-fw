#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <driver/spi_master.h>

// Native ESP-IDF facilities shared by the device services. This is not an
// Arduino emulation layer: drivers own their ports and all lengths are explicit.
namespace platform {
bool begin(); // USB console only; NVS initialization remains explicit in app_main.
uint32_t millis();
void delay(uint32_t milliseconds);
size_t freeHeap();
size_t minimumFreeHeap();
size_t psramSize();
size_t freePsram();
[[noreturn]] void restart();

class Console {
public:
    void begin(unsigned baud=115200);
    explicit operator bool()const{return ready_;}
    int available();
    int read();
    size_t write(const uint8_t* bytes,size_t size);
    size_t write(const char* text);
    void print(const char* text){write(text);}
    void print(const std::string& text){write(reinterpret_cast<const uint8_t*>(text.data()),text.size());}
    template<class T,typename std::enable_if<std::is_arithmetic<T>::value,int>::type=0>
    void print(T value){print(std::to_string(value));}
    void println(){write("\n");}
    template<class T> void println(const T& value){print(value);println();}
    int printf(const char* format,...);
private:
    bool ready_=false;
    int pending_=-1;
};
extern Console console;

class Uart {
public:
    void setRxBufferSize(size_t size){rxSize_=size;}
    bool begin(unsigned baud,int rx,int tx);
    int available();
    int read();
    size_t write(const uint8_t* bytes,size_t size);
private:
    size_t rxSize_=8192;
    bool ready_=false;
};
extern Uart modemUart;

void gpioOutput(int pin,bool level);
void gpioInput(int pin,bool pullUp=false,bool pullDown=false);
void gpioWrite(int pin,bool level);
int gpioRead(int pin);
bool i2cBegin(int sda,int scl,uint32_t hz=400000);
bool i2cProbe(uint8_t address);
bool i2cWrite(uint8_t address,const uint8_t* bytes,size_t size);
bool i2cRead(uint8_t address,uint8_t* bytes,size_t size);
bool i2cWriteRead(uint8_t address,const uint8_t* tx,size_t txSize,uint8_t* rx,size_t rxSize);
bool spiBegin(int sck,int miso,int mosi);
spi_device_handle_t spiDevice(int cs,uint32_t hz=4000000,uint8_t mode=0);
bool spiTransfer(spi_device_handle_t device,const uint8_t* tx,uint8_t* rx,size_t size);
bool spiAcquire(spi_device_handle_t device);
void spiRelease(spi_device_handle_t device);
}
