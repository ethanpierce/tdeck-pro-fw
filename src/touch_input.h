#pragma once
#include <cstdint>
#include <touch_packet.h>
class TouchInput {
public:
    bool begin();
    bool poll(int& x,int& y);
    bool ready=false;
    const char* model()const{return !ready?"Unavailable":controller_==deck::TouchController::Cst3530?"CST3530":"CST328";}
private:
    deck::TouchController controller_=deck::TouchController::Cst328;
    bool pressed_=false;
    uint32_t polledAt_=0;
};
