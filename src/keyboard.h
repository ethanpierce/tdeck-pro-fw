#pragma once
#include <Adafruit_TCA8418.h>
#include <keyboard_decoder.h>
class Keyboard {
public:
    bool begin();
    char poll(); // ASCII, '\b', '\r', or ESC (Sym key = home).
    bool ready = false;
    bool trace = false;
private:
    Adafruit_TCA8418 keypad_;
    deck::KeyboardDecoder decoder_;
};
