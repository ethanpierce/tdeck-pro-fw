#pragma once
#include <Adafruit_TCA8418.h>
#include <keyboard_decoder.h>
class Keyboard {
public:
    bool begin();
    char poll(); // ASCII, backspace/Enter/ESC; 17/18 = Shift+Alt+K/J thread scroll.
    bool ready = false;
    bool trace = false;
private:
    Adafruit_TCA8418 keypad_;
    deck::KeyboardDecoder decoder_;
};
