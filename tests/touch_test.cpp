#include <touch_packet.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
namespace {
void checksum(std::vector<uint8_t>& packet) {
    unsigned sum=0x55;
    const unsigned count=packet[3]&15;
    for(unsigned n=4;n<4+count*5;++n)sum+=packet[n];
    packet[0]=uint8_t(sum);packet[1]=uint8_t(sum>>8);
}
}
int main() {
    using namespace deck;
    uint8_t old[]={6,14,19,0xff,40,1,0xab};
    auto p=touchPacket(TouchController::Cst328,old,7);
    assert(p.valid && p.down && p.x==239 && p.y==319);
    old[0]=0;p=touchPacket(TouchController::Cst328,old,7);assert(p.valid && !p.down);
    old[0]=6;old[1]=15;assert(!touchPacket(TouchController::Cst328,old,7).valid);
    old[1]=14;old[5]=6;assert(!touchPacket(TouchController::Cst328,old,7).valid);
    old[5]=0x81;assert(!touchPacket(TouchController::Cst328,old,7).valid);
    old[5]=2;p=touchPacket(TouchController::Cst328,old,7);assert(p.valid && !p.down);
    old[5]=1;old[6]=0;assert(!touchPacket(TouchController::Cst328,old,7).valid);
    std::vector<uint8_t> modern={0,0,0xff,1,239,63,40,0x10,0x10};checksum(modern);
    p=touchPacket(TouchController::Cst3530,modern.data(),modern.size());
    assert(p.valid && p.down && p.x==239 && p.y==319);
    modern[8]=0;checksum(modern);p=touchPacket(TouchController::Cst3530,modern.data(),modern.size());assert(p.valid && !p.down);
    modern[8]=0x10;modern[4]=240;checksum(modern);assert(!touchPacket(TouchController::Cst3530,modern.data(),modern.size()).valid);
    modern[4]=239;checksum(modern);modern[0]^=1;assert(!touchPacket(TouchController::Cst3530,modern.data(),modern.size()).valid);
    modern={0x55,0,0xff,0,0,0,0,0,0};p=touchPacket(TouchController::Cst3530,modern.data(),modern.size());assert(p.valid && !p.down);
    modern[3]=6;assert(!touchPacket(TouchController::Cst3530,modern.data(),modern.size()).valid);
    modern[3]=0x11;assert(!touchPacket(TouchController::Cst3530,modern.data(),modern.size()).valid);
    assert(!touchPacket(TouchController::Cst328,nullptr,0).valid);
    std::mt19937 rng(0x544f5543);
    for(unsigned n=0;n<6000;++n) {
        std::vector<uint8_t> bytes(rng()%40);for(auto& b:bytes)b=uint8_t(rng());
        for(const auto controller:{TouchController::Cst328,TouchController::Cst3530}) {
            const auto sample=touchPacket(controller,bytes.data(),bytes.size());
            assert(!sample.down || (sample.valid && sample.x<240 && sample.y<320));
        }
    }
    std::cout<<"PASS bounded touch packets, checksum, press/release, coordinates and 6000 malformed inputs\n";
}
