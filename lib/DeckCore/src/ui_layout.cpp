#include "ui_layout.h"
#include "mono_canvas.h"
#include <algorithm>
namespace deck {
namespace {
void label(MonoCanvas& c,int x,int y,std::string_view value,int size=1){c.setTextSize(size);c.setCursor(x,y);c.print(value);}
}
void drawUiButton(MonoCanvas& c,UiRect r,std::string_view value,bool filled) {
    if(filled)c.fillRoundRect(r.x,r.y,r.width,r.height,4,Ink);else c.drawRoundRect(r.x,r.y,r.width,r.height,4,Ink);
    c.setTextColor(filled?Paper:Ink);label(c,r.x+(r.width-int(value.size())*6)/2,r.y+(r.height-8)/2,value);c.setTextColor(Ink);
}
void drawHomeRow(MonoCanvas& c,unsigned index,std::string_view title,std::string_view detail,char shortcut,bool selected) {
    const auto r=homeRow(index);
    if(selected)c.fillRoundRect(r.x,r.y,r.width,r.height,7,Ink);else c.drawRoundRect(r.x,r.y,r.width,r.height,7,Ink);
    c.setTextColor(selected?Paper:Ink);label(c,22,r.y+9,title,2);label(c,22,r.y+34,detail.substr(0,33));label(c,216,r.y+12,std::string_view(&shortcut,1));c.setTextColor(Ink);
}
void drawSettingRow(MonoCanvas& c,unsigned index,std::string_view title,std::string_view detail,char shortcut,bool selected) {
    const auto r=settingsRow(index);
    if(selected)c.fillRoundRect(r.x,r.y,r.width,r.height,3,Ink);else c.drawFastHLine(r.x,r.y+r.height-1,r.width,Ink);
    c.setTextColor(selected?Paper:Ink);label(c,r.x+7,r.y+3,title,2);label(c,r.x+7,r.y+22,detail.substr(0,33));label(c,220,r.y+5,std::string_view(&shortcut,1));c.setTextColor(Ink);
}
void drawPhonePad(MonoCanvas& c) {
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<3;++column){const auto r=phoneKey(row,column);const char key=PhoneKeys[row*3+column];c.drawRoundRect(r.x,r.y,r.width,r.height,5,Ink);label(c,r.x+29,r.y+7,std::string_view(&key,1),2);}
}
}
