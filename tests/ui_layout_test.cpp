#include <ui_layout.h>
#include <chat_lines.h>
#include <cassert>
#include <iostream>
int main(){
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col){const auto r=deck::phoneKey(row,col);const char expected=deck::PhoneKeys[row*3+col];
        assert(deck::phoneKeyAt(r.x,r.y)==expected && deck::phoneKeyAt(r.x+r.width-1,r.y+r.height-1)==expected);
        assert(deck::phoneKeyAt(r.x+r.width,r.y)==0 && deck::phoneKeyAt(r.x,r.y+r.height)==0);
    }
    for(int y=0;y<320;++y)for(int x=0;x<240;++x){
        if(deck::PhonePrimary.contains(x,y) || deck::PhoneEnd.contains(x,y) || deck::PhoneErase.contains(x,y) || deck::PhonePlus.contains(x,y))assert(!deck::phoneKeyAt(x,y));
    }
    assert(!deck::phoneKeyAt(-1,-1) && !deck::phoneKeyAt(240,320));
    for(unsigned n=0;n<3;++n){const auto row=deck::homeRow(n);assert(row.y+row.height<=273);if(n)assert(!row.contains(20,deck::homeRow(n-1).y+53));}
    for(unsigned n=0;n<6;++n){const auto row=deck::settingsRow(n);assert(row.y+row.height<=273);}
    deck::UiChatLines lines;
    for(unsigned n=0;n<16;++n)deck::appendChatBubble(lines,"Peer",std::string(480,'\n'),false,n);
    assert(lines.size()==deck::UiChatLineLimit && lines.back().gap);
    deck::appendChatBubble(lines,"You","Final message",true,99);
    assert(lines.size()==deck::UiChatLineLimit && lines[lines.size()-2].value=="Final message");
    assert(lines[lines.size()-2].outgoing && lines[lines.size()-2].group==99);
    for(const auto& line:lines)assert(line.value.size()<=30);
    deck::UiChatLines utf8;deck::appendChatBubble(utf8,"AI","Hello \xc3\xa9\r\nnext",false,0);
    assert(utf8[1].value=="Hello ?" && utf8[2].value=="next");
    std::cout<<"PASS dialpad digits/gutters and separate explicit call targets; Home/Settings bounds; bounded chat history\n";
}
