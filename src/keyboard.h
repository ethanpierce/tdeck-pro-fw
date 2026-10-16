#pragma once
#include <keyboard_decoder.h>
class Keyboard {
public:
    bool begin();
    char poll(); // ASCII, backspace/Enter/ESC; 17/18 = Shift+Alt+K/J thread scroll.
    bool ready=false,trace=false,lostEvents=false;
private:
    deck::KeyboardDecoder decoder_;
    void flush();
};
