#include "native_text.h"
#include "native_screen.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
namespace {
uint32_t tick=0;
std::map<int,bool> pins;
unsigned acquired=0,busyReads=0,callbacks=0;
bool stuckBusy=false,failTransfer=false;
struct Command {uint8_t value;std::vector<uint8_t> data;};
std::vector<Command> commands;
const Command& last(uint8_t code) {for(auto i=commands.rbegin();i!=commands.rend();++i)if(i->value==code)return *i;std::abort();}
size_t count(uint8_t code){size_t n=0;for(const auto& c:commands)if(c.value==code)++n;return n;}
void reset(){commands.clear();busyReads=0;callbacks=0;failTransfer=false;stuckBusy=false;}
void onBusy(const void*){++callbacks;platform::delay(1);}
bool ink(const deck::MonoCanvas& canvas,int x,int y){return !(canvas.pixels()[y*30+x/8]&(0x80>>(x&7)));}
}
namespace platform {
Console console;
uint32_t millis(){return tick;}
void delay(uint32_t ms){tick+=ms;}
void gpioOutput(int pin,bool level){pins[pin]=level;}
void gpioInput(int,bool,bool){}
void gpioWrite(int pin,bool level){pins[pin]=level;}
int gpioRead(int){if(stuckBusy)return 0;if(busyReads){--busyReads;return 0;}return 1;}
spi_device_handle_t spiDevice(int cs,uint32_t hz,uint8_t mode){assert(cs==-1 && hz==4000000 && mode==0);return reinterpret_cast<void*>(1);}
bool spiAcquire(spi_device_handle_t){assert(!acquired);++acquired;return true;}
void spiRelease(spi_device_handle_t){assert(acquired==1);--acquired;}
bool spiTransfer(spi_device_handle_t,const uint8_t* bytes,uint8_t*,size_t size){
    assert(acquired==1 && !pins[34]);assert(size && size<=3840);
    if(failTransfer){failTransfer=false;return false;}
    if(!pins[35]) {assert(size==1);commands.push_back({bytes[0],{}});if(bytes[0]==0x12 || bytes[0]==0x04)busyReads=3;}
    else {assert(!commands.empty());commands.back().data.insert(commands.back().data.end(),bytes,bytes+size);}
    return true;
}
}
int main() {
    Text text=" ai key hidden \r\n";text.trim();assert(text=="ai key hidden");
    assert(text.startsWith("ai key ") && text.substring(7)=="hidden");
    assert(text.indexOf(' ',3)==6 && text.lastIndexOf(' ')==6 && text.indexOf("missing")==-1);
    assert(Text(42)+"%"=="42%" && Text("RAT ")+7=="RAT 7" && "IP "+Text("a")=="IP a");
    text.remove(7);assert(text=="ai key ");assert(Text("00123").toInt()==123);
    const std::string native="Wi-Fi";Text copied=native;const std::string& shared=copied;assert(shared==native);
    deck::MonoCanvas canvas;
    for(unsigned n=0;n<9600;++n)assert(canvas.pixels()[n]==0xff);
    canvas.drawPixel(0,0,deck::Ink);canvas.drawPixel(239,319,deck::Ink);
    assert(canvas.pixels()[0]==0x7f && canvas.pixels()[9599]==0xfe);
    canvas.drawPixel(-1,-1,deck::Ink);canvas.drawPixel(240,320,deck::Ink);
    canvas.fillScreen(deck::Paper);canvas.fillRect(-2,-2,4,4,deck::Ink);
    assert(ink(canvas,0,0) && ink(canvas,1,1) && !ink(canvas,2,2));
    canvas.fillScreen(deck::Paper);canvas.drawRoundRect(10,10,20,15,4,deck::Ink);
    assert(!ink(canvas,10,10) && ink(canvas,14,10) && ink(canvas,10,14) && !ink(canvas,18,17));
    canvas.fillRoundRect(50,50,20,15,4,deck::Ink);assert(ink(canvas,58,57));
    canvas.setCursor(100,100);canvas.setTextSize(2);canvas.print("A");assert(ink(canvas,102,100) || ink(canvas,104,100));
    board::Screen screen(34,35,16,37);assert(screen.begin());screen.setBusyCallback(onBusy);
    screen.fillScreen(deck::Paper);screen.drawPixel(0,0,deck::Ink);
    screen.setPartialWindow(0,214,240,54);assert(screen.refresh()); // First always full
    assert(count(0x10)==2 && count(0x13)==1 && count(0x12)==1);
    assert(last(0x13).data.size()==9600 && last(0x13).data[0]==0x7f);
    assert(last(0x10).data==last(0x13).data && last(0xe5).data==std::vector<uint8_t>{0x5a});
    assert(callbacks && !acquired && pins[34]);
    reset();screen.setPartialWindow(0,214,240,54);assert(screen.refresh());
    assert(count(0x10)==1 && count(0x13)==1 && last(0x13).data.size()==1620);
    assert(last(0xe5).data==std::vector<uint8_t>{0x79});
    assert(last(0x90).data==std::vector<uint8_t>({0,239,0,214,1,11,1}));
    reset();screen.setPartialWindow(7,10,2,3);assert(screen.refresh());
    assert(last(0x13).data.size()==6); // Byte-align 7..8 to 0..15, 3 rows
    reset();failTransfer=true;assert(!screen.refresh());assert(!acquired && pins[34]);
    reset();stuckBusy=true;const auto before=tick;assert(!screen.refresh());assert(tick-before>10000 && !acquired && pins[34]);
    std::cout<<"Native UI / display protocol tests passed\n";
}
