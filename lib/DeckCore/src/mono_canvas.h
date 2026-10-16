#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace deck {
constexpr uint8_t Ink=0,Paper=1;
// Fixed 240x320 framebuffer, MSB first, one bit per pixel. No device APIs.
class MonoCanvas {
public:
    static constexpr int Width=240,Height=320,Stride=Width/8;
    MonoCanvas(){fillScreen(Paper);}
    void fillScreen(uint8_t color);
    void drawPixel(int x,int y,uint8_t color);
    void drawFastHLine(int x,int y,int width,uint8_t color);
    void fillRect(int x,int y,int width,int height,uint8_t color);
    void drawRect(int x,int y,int width,int height,uint8_t color);
    void drawRoundRect(int x,int y,int width,int height,int radius,uint8_t color);
    void fillRoundRect(int x,int y,int width,int height,int radius,uint8_t color);
    void setTextSize(int size){textSize_=size>0?size:1;}
    void setCursor(int x,int y){cursorX_=x;cursorY_=y;}
    void setTextColor(uint8_t color){textColor_=color;}
    void print(std::string_view text);
    const uint8_t* pixels()const{return data_.data();}
protected:
    std::array<uint8_t,Stride*Height> data_{};
private:
    int cursorX_=0,cursorY_=0,textSize_=1;
    uint8_t textColor_=Ink;
    void roundShape(int x,int y,int width,int height,int radius,uint8_t color,bool filled);
};
}
