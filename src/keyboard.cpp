#include "keyboard.h"
#include <Wire.h>
bool Keyboard::begin() {
    ready=keypad_.begin(0x34,&Wire);
    if(ready){keypad_.matrix(4,10);keypad_.flush();}
    return ready;
}
char Keyboard::poll() {
    if(!ready) return 0;
    while(keypad_.available()) {
        uint8_t raw=keypad_.getEvent();
        if(trace) Serial.printf("key raw=%u number=%u %s\n",raw,raw & 127,raw & 128?"down":"up");
        char c=decoder_.feed(raw);
        if(c) return c;
    }
    return 0;
}
