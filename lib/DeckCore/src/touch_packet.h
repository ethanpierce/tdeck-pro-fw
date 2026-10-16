#pragma once
#include <cstddef>
#include <cstdint>
namespace deck {
enum class TouchController { Cst328, Cst3530 };
struct TouchSample { bool valid=false, down=false; uint16_t x=0,y=0; };
// Packet formats follow LILYGO's pinned Hynitron examples, without firmware
// update, calibration, gesture or multi-finger actions.
TouchSample touchPacket(TouchController controller,const uint8_t* data,size_t size);
}
