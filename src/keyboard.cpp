// TCA8418 register initialization follows Adafruit_TCA8418 (BSD-licensed),
// Copyright Adafruit Industries; notice: docs/licenses/Adafruit-TCA8418.txt.
// Native I2C transactions, no Adafruit BusIO or framework dependency.
#include "keyboard.h"
#include <platform.h>
#include <utility>
namespace {
constexpr uint8_t Address=0x34;
bool writeRegister(uint8_t reg,uint8_t value){const uint8_t bytes[]={reg,value};return platform::i2cWrite(Address,bytes,2);}
bool readRegister(uint8_t reg,uint8_t& value){return platform::i2cWriteRead(Address,&reg,1,&value,1);}
}
void Keyboard::flush() {
    uint8_t value=0;
    for(unsigned n=0;n<16;++n)if(!readRegister(0x04,value) || !value)break;
    for(uint8_t reg=0x11;reg<=0x13;++reg)readRegister(reg,value);
    writeRegister(0x02,0x1f);
    decoder_=deck::KeyboardDecoder{};
}
bool Keyboard::begin() {
    ready=false;lostEvents=false;
    if(!platform::i2cProbe(Address))return false;
    constexpr std::pair<uint8_t,uint8_t> init[]={
        {0x23,0x00},{0x24,0x00},{0x25,0x00}, // GPIO inputs
        {0x20,0xff},{0x21,0xff},{0x22,0xff}, // GPIO key events
        {0x26,0x00},{0x27,0x00},{0x28,0x00}, // Falling-level interrupts
        {0x1a,0xff},{0x1b,0xff},{0x1c,0xff},
        {0x1d,0x0f},{0x1e,0xff},{0x1f,0x03} // 4 rows x 10 columns
    };
    for(const auto& reg:init)if(!writeRegister(reg.first,reg.second))return false;
    flush();ready=true;return true;
}
char Keyboard::poll() {
    if(!ready)return 0;
    uint8_t status=0,count=0;
    if(!readRegister(0x02,status))return 0;
    if(status&0x08){lostEvents=true;flush();return 0;}
    // The hardware FIFO is ten events deep. Bound every pass in case of an
    // I2C fault; release/modifier events still reach the pure decoder.
    for(unsigned n=0;n<16;++n) {
        if(!readRegister(0x03,count) || !(count&0x0f))break;
        uint8_t raw=0;if(!readRegister(0x04,raw))break;
        if(trace)platform::console.printf("key raw=%u number=%u %s\n",raw,raw&127,raw&128?"down":"up");
        const char c=decoder_.feed(raw);if(c)return c;
    }
    return 0;
}
