#pragma once
#include <mono_canvas.h>
#include <platform.h>
namespace board {
// Native UC8253 / GDEQ031T10 display, using panel OTP waveforms. The fixed
// framebuffer and canvas have no dependency on a graphics/framework runtime.
class Screen : public deck::MonoCanvas {
public:
    Screen(int cs,int dc,int reset,int busy):cs_(cs),dc_(dc),reset_(reset),busy_(busy){}
    bool begin();
    void setBusyCallback(void (*callback)(const void*)){busyCallback_=callback;}
    void setFullWindow(){partial_=false;x_=y_=0;width_=Width;height_=Height;}
    void setPartialWindow(int x,int y,int width,int height);
    bool refresh();
    bool shutdown(){return powerOff();}
private:
    int cs_,dc_,reset_,busy_;
    int x_=0,y_=0,width_=Width,height_=Height;
    bool partial_=false,initialized_=false,powered_=false,first_=true,offConfirmed_=false;
    spi_device_handle_t device_=nullptr;
    void (*busyCallback_)(const void*)=nullptr;
    bool command(uint8_t command,const uint8_t* data=nullptr,size_t size=0);
    bool waitReady();
    bool initializePanel();
    bool window();
    bool writePlane(uint8_t command);
    bool update(bool partial);
    bool powerOff();
};
}
