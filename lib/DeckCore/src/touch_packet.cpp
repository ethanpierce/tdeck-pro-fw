#include "touch_packet.h"
namespace deck {
TouchSample touchPacket(TouchController controller,const uint8_t* p,size_t size) {
    TouchSample sample;
    if(!p)return sample;
    if(controller==TouchController::Cst328) {
        if(size!=7 || p[6]!=0xab)return sample;
        unsigned count=p[5]&0x7f;
        if(count>5 || (p[5]&0x80))return sample;
        sample.valid=true;
        if(!count || p[0]==0xab)return sample;
        if(count!=1)return sample;
        sample.down=(p[0]&0x0f)==6;
        sample.x=(uint16_t(p[1])<<4)|(p[3]>>4);
        sample.y=(uint16_t(p[2])<<4)|(p[3]&15);
    } else {
        if(size<9 || p[2]!=0xff)return sample;
        unsigned count=p[3]&15,keys=p[3]>>4;
        if(count>5 || keys || size!=9+(count?count-1:0)*5)return sample;
        uint16_t checksum=0x55;
        for(size_t n=4;n<4+count*5;++n)checksum=uint16_t(checksum+p[n]);
        if(checksum!=(uint16_t(p[0])|(uint16_t(p[1])<<8)))return sample;
        sample.valid=true;
        if(count!=1)return sample;
        sample.down=(p[8]>>4)!=0;
        sample.x=p[4]|(uint16_t(p[7]&15)<<8);
        sample.y=p[5]|(uint16_t(p[7]&0xf0)<<4);
    }
    if(sample.down && (sample.x>=240 || sample.y>=320))return {};
    return sample;
}
}
