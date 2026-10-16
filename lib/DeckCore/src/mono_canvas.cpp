#include "mono_canvas.h"
#include "font5x7.h"
#include <algorithm>
namespace deck {
void MonoCanvas::fillScreen(uint8_t color){data_.fill(color?0xff:0);}
void MonoCanvas::drawPixel(int x,int y,uint8_t color) {
    if(x<0 || y<0 || x>=Width || y>=Height)return;
    auto& byte=data_[size_t(y)*Stride+size_t(x)/8];const uint8_t mask=0x80>>(x&7);
    if(color)byte|=mask;else byte&=uint8_t(~mask);
}
void MonoCanvas::drawFastHLine(int x,int y,int width,uint8_t color){fillRect(x,y,width,1,color);}
void MonoCanvas::fillRect(int x,int y,int width,int height,uint8_t color) {
    if(width<=0 || height<=0)return;
    const int right=std::min(Width,x+width),bottom=std::min(Height,y+height);
    for(int row=std::max(0,y);row<bottom;++row)for(int col=std::max(0,x);col<right;++col)drawPixel(col,row,color);
}
void MonoCanvas::drawRect(int x,int y,int width,int height,uint8_t color) {
    if(width<=0 || height<=0)return;
    drawFastHLine(x,y,width,color);drawFastHLine(x,y+height-1,width,color);
    fillRect(x,y,1,height,color);fillRect(x+width-1,y,1,height,color);
}
void MonoCanvas::roundShape(int x,int y,int width,int height,int radius,uint8_t color,bool filled) {
    if(width<=0 || height<=0)return;
    radius=std::max(0,std::min(radius,std::min(width,height)/2));
    if(!radius){if(filled)fillRect(x,y,width,height,color);else drawRect(x,y,width,height,color);return;}
    for(int row=0;row<height;++row) {
        int inset=0;
        const int corner=row<radius?radius-1-row:row>=height-radius?row-(height-radius):0;
        if(row<radius || row>=height-radius) {
            while(inset<radius && (radius-inset)*(radius-inset)+corner*corner>radius*radius)++inset;
        }
        if(filled || row==0 || row==height-1)drawFastHLine(x+inset,y+row,width-2*inset,color);
        else {drawPixel(x+inset,y+row,color);drawPixel(x+width-1-inset,y+row,color);}
    }
}
void MonoCanvas::drawRoundRect(int x,int y,int width,int height,int radius,uint8_t color){roundShape(x,y,width,height,radius,color,false);}
void MonoCanvas::fillRoundRect(int x,int y,int width,int height,int radius,uint8_t color){roundShape(x,y,width,height,radius,color,true);}
void MonoCanvas::print(std::string_view text) {
    for(unsigned char ch:text) {
        if(ch=='\r')continue;
        if(ch=='\n'){cursorX_=0;cursorY_+=8*textSize_;continue;}
        if(ch<32 || ch>126)ch='?';
        const auto* glyph=Font5x7+(ch-32)*5;
        for(int col=0;col<5;++col)for(int row=0;row<8;++row)if(glyph[col]&(1u<<row))fillRect(cursorX_+col*textSize_,cursorY_+row*textSize_,textSize_,textSize_,textColor_);
        cursorX_+=6*textSize_;
    }
}
}
