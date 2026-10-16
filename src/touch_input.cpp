#include "touch_input.h"
#include "board.h"
#include <Wire.h>
#include <driver/gpio.h>
namespace {
constexpr uint8_t Address=0x1a;
constexpr int Interrupt=12;
bool command(uint32_t value,unsigned bytes) {
    Wire.beginTransmission(Address);
    for(unsigned n=bytes;n;--n)Wire.write(uint8_t(value>>((n-1)*8)));
    return Wire.endTransmission()==0;
}
bool readBytes(uint8_t* data,unsigned count) {
    if(Wire.requestFrom(Address,uint8_t(count))!=count){while(Wire.available())Wire.read();return false;}
    for(unsigned n=0;n<count;++n)data[n]=uint8_t(Wire.read());
    return true;
}
bool readRegister(uint32_t reg,unsigned width,uint8_t* data,unsigned count) {
    return command(reg,width) && readBytes(data,count);
}
void resetTouch() {
    const int reset=board::revision11?38:45;
    gpio_hold_dis(static_cast<gpio_num_t>(reset));
    digitalWrite(reset,LOW);pinMode(reset,OUTPUT);delay(10);
    digitalWrite(reset,HIGH);delay(50);
}
bool modernMode() {
    if(!command(0xd0000400,4))return false;
    delay(1);
    return command(0xd0000400,4) && command(0xd0000000,4) && command(0xd0000c00,4) && command(0xd0000100,4);
}
}
bool TouchInput::begin() {
    ready=false;pressed_=false;
    if(board::unsupported)return false;
    // Keep all I2C operations on the UI task. Gate reads on the latched IRQ
    // level, including CST3530 sleep/wake, without installing a GPIO ISR.
    pinMode(Interrupt,INPUT_PULLUP);
    resetTouch();
    uint8_t info[50]{};
    if(modernMode() && readRegister(0xd0030000,4,info,sizeof(info)) && info[2]==0xca && info[3]==0xca) {
        const unsigned width=info[28]|(unsigned(info[29])<<8),height=info[30]|(unsigned(info[31])<<8);
        Serial.printf("Touch controller: CST3530 %ux%u\n",width,height);
        if(width==240 && height==320){controller_=deck::TouchController::Cst3530;ready=true;}
    }
    if(!ready) {
        resetTouch();
        if(command(0xd101,2)){delay(1);
            if(readRegister(0xd1f4,2,info,28)) {
                const unsigned width=info[4]|(unsigned(info[5])<<8),height=info[6]|(unsigned(info[7])<<8);
                Serial.printf("Touch controller: CST328 %ux%u\n",width,height);
                if(width==240 && height==320 && command(0xd109,2)){controller_=deck::TouchController::Cst328;ready=true;}
            }
        }
    }
    Serial.printf("Touch: %s\n",model());
    return ready;
}
bool TouchInput::poll(int& x,int& y) {
    if(!ready || uint32_t(millis()-polledAt_)<20 || digitalRead(Interrupt)!=LOW)return false;
    polledAt_=millis();
    uint8_t packet[29]{};unsigned size=controller_==deck::TouchController::Cst3530?9:7;
    bool ok=readRegister(controller_==deck::TouchController::Cst3530?0xd0070000:0xd000,
        controller_==deck::TouchController::Cst3530?4:2,packet,size);
    if(ok && controller_==deck::TouchController::Cst3530) {
        const unsigned count=(packet[3]&15)+(packet[3]>>4);
        if(count>5)ok=false;
        else if(count>1){ok=readBytes(packet+9,(count-1)*5);size+=(count-1)*5;}
    }
    // Acknowledge the packet even when it was malformed; never infer a tap
    // from an I2C failure or an unsupported coordinate range.
    const bool acknowledged=controller_==deck::TouchController::Cst3530?command(0xd00002ab,4):command(0xd000ab,3);
    if(!ok || !acknowledged)return false;
    const auto point=deck::touchPacket(controller_,packet,size);
    if(!point.valid)return false;
    const bool tap=point.down && !pressed_;
    pressed_=point.down;
    if(!tap)return false;
    x=point.x;y=point.y;return true;
}
