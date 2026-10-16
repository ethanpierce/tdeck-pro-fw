// SPDX-License-Identifier: GPL-3.0-only
// Native ESP-IDF adaptation of the GDEQ031T10 / UC8253 command protocol in
// GxEPD2_310_GDEQ031T10.cpp, Copyright Jean-Marc Zingg, GxEPD2 (GPL-3.0).
// https://github.com/ZinggJM/GxEPD2 (vendored protocol source retained in Git
// history; license retained at lib/GxEPD2/LICENSE). Based on Good Display's
// GDEQ031T10 example. Panel settings and OTP refresh commands are unchanged.
#include "native_screen.h"
#include <algorithm>
namespace board {
bool Screen::begin() {
    platform::gpioOutput(cs_,true);platform::gpioOutput(dc_,true);
    platform::gpioInput(busy_);
    if(reset_>=0) {
        platform::gpioOutput(reset_,true);platform::delay(10);
        platform::gpioWrite(reset_,false);platform::delay(2);
        platform::gpioWrite(reset_,true);platform::delay(10);
    }
    device_=platform::spiDevice(-1,4000000,0);
    return device_!=nullptr;
}
void Screen::setPartialWindow(int x,int y,int width,int height) {
    partial_=true;
    const int right=std::clamp(x+width,0,Width),bottom=std::clamp(y+height,0,Height);
    x_=std::clamp(x,0,Width)&~7;y_=std::clamp(y,0,Height);
    width_=std::max(0,std::min(Width,(right+7)&~7)-x_);height_=std::max(0,bottom-y_);
}
bool Screen::command(uint8_t value,const uint8_t* bytes,size_t size) {
    if(!device_ || !platform::spiAcquire(device_))return false;
    platform::gpioWrite(dc_,false);platform::gpioWrite(cs_,false);
    bool ok=platform::spiTransfer(device_,&value,nullptr,1);
    platform::gpioWrite(cs_,true);
    if(ok && size) {
        platform::gpioWrite(dc_,true);platform::gpioWrite(cs_,false);
        ok=platform::spiTransfer(device_,bytes,nullptr,size);
        platform::gpioWrite(cs_,true);
    }
    platform::spiRelease(device_);return ok;
}
bool Screen::waitReady() {
    platform::delay(1);const uint32_t start=platform::millis();
    // The panel's BUSY line is active low. Drain physical key events during
    // its refresh without rendering, changing app state, or touching SPI.
    while(platform::gpioRead(busy_)==0) {
        if(busyCallback_)busyCallback_(nullptr);else platform::delay(1);
        if(uint32_t(platform::millis()-start)>10000){platform::console.println("Display busy timeout");return false;}
    }
    return true;
}
bool Screen::initializePanel() {
    if(initialized_)return true;
    const uint8_t reset[]={0x1e,0x0d},settings[]={0x1f,0x0d};
    if(!command(0x00,reset,sizeof(reset)))return false;
    platform::delay(1);powered_=false;
    if(!command(0x00,settings,sizeof(settings)))return false;
    initialized_=true;return true;
}
bool Screen::window() {
    const int right=x_+width_-1,bottom=y_+height_-1;
    const uint8_t bytes[]={uint8_t(x_),uint8_t(right),uint8_t(y_>>8),uint8_t(y_),uint8_t(bottom>>8),uint8_t(bottom),0x01};
    return command(0x90,bytes,sizeof(bytes));
}
bool Screen::writePlane(uint8_t plane) {
    if(!initializePanel() || !command(0x91) || !window() || !command(plane))return false;
    if(!platform::spiAcquire(device_))return false;
    platform::gpioWrite(dc_,true);platform::gpioWrite(cs_,false);
    bool ok=true;
    // Whole rows are contiguous for full-width updates; submit DMA-sized
    // chunks instead of one transaction per pixel byte.
    if(width_==Width) {
        const uint8_t* bytes=pixels()+y_*Stride;size_t remaining=size_t(height_)*Stride;
        while(remaining && ok){const size_t size=std::min<size_t>(remaining,3840);ok=platform::spiTransfer(device_,bytes,nullptr,size);bytes+=size;remaining-=size;}
    } else {
        for(int y=y_;y<y_+height_ && ok;++y)ok=platform::spiTransfer(device_,pixels()+y*Stride+x_/8,nullptr,size_t(width_/8));
    }
    platform::gpioWrite(cs_,true);platform::spiRelease(device_);
    return command(0x92) && ok;
}
bool Screen::update(bool partial) {
    if(partial && (!command(0x91) || !window()))return false;
    const uint8_t cascade=0x02,temperature=partial?0x79:0x5a,border=partial?0xd7:0x97;
    if(!command(0xe0,&cascade,1) || !command(0xe5,&temperature,1) || !command(0x50,&border,1))return false;
    if(!powered_) {
        if(!command(0x04) || !waitReady())return false;
        powered_=true;
    }
    if(!command(0x12) || !waitReady())return false;
    initialized_=false;
    return !partial || command(0x92);
}
bool Screen::powerOff() {
    if(powered_ && (!command(0x02) || !waitReady()))return false;
    powered_=false;return true;
}
bool Screen::refresh() {
    if(width_<=0 || height_<=0)return true;
    // First refresh must cover the whole panel. Main also schedules one full
    // clean every 16 frames to limit partial-update ghosting.
    const bool partial=partial_ && !first_;
    if(!partial){x_=y_=0;width_=Width;height_=Height;}
    bool ok=(!partial?writePlane(0x10):true) && writePlane(0x13) && update(partial) && writePlane(0x10);
    if(!partial)ok=powerOff() && ok;
    if(ok)first_=false;
    return ok;
}
}
