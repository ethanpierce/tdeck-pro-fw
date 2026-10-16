#pragma once
#include <cstdint>
namespace deck {
class KeyboardDecoder {
public:
    char feed(uint8_t event);
private:
    bool alt_=false, leftShift_=false, rightShift_=false;
};
}
