#pragma once
#include <array>
namespace deck {
struct UiRect {
    int x,y,width,height;
    constexpr bool contains(int px,int py)const{return px>=x && py>=y && px<x+width && py<y+height;}
};
constexpr UiRect homeRow(unsigned row){return {10,83+int(row)*60,220,54};}
constexpr UiRect settingsRow(unsigned row){return {10,76+int(row)*32,220,31};}
constexpr UiRect phoneKey(unsigned row,unsigned column){return {10+int(column)*75,116+int(row)*33,70,29};}
constexpr std::array<char,12> PhoneKeys={'1','2','3','4','5','6','7','8','9','*','0','#'};
constexpr char phoneKeyAt(int x,int y) {
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<3;++column)
        if(phoneKey(row,column).contains(x,y))return PhoneKeys[row*3+column];
    return 0;
}
constexpr UiRect PhoneErase{204,83,26,25},PhonePlus{10,83,24,25};
constexpr UiRect PhonePrimary{10,251,144,21},PhoneEnd{164,251,66,21};
}
#include <string_view>
namespace deck {
class MonoCanvas;
void drawUiButton(MonoCanvas&,UiRect,std::string_view,bool filled=false);
void drawHomeRow(MonoCanvas&,unsigned,std::string_view,std::string_view,char,bool selected);
void drawSettingRow(MonoCanvas&,unsigned,std::string_view,std::string_view,char,bool selected);
void drawPhonePad(MonoCanvas&);
}
