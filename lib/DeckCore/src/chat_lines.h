#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
namespace deck {
struct UiChatLine {std::string value;unsigned group;bool outgoing;bool gap=false;};
using UiChatLines=std::deque<UiChatLine>;
constexpr size_t UiChatLineLimit=300;
inline void appendChatBubble(UiChatLines& lines,std::string_view sender,std::string_view body,bool outgoing,unsigned group) {
    auto append=[&](std::string value,bool gap=false){if(lines.size()==UiChatLineLimit)lines.pop_front();lines.push_back({std::move(value),group,outgoing,gap});};
    append(std::string(sender.substr(0,30)));
    std::string clean;clean.reserve(body.size());
    for(unsigned char c:body){if(c=='\n' || (c>=32 && c<127))clean+=char(c);else if(c>=0xc0)clean+='?';else if(c<32 && c!='\r')clean+=' ';}
    size_t at=0;
    if(clean.empty())append("");
    while(at<clean.size()) {
        size_t end=std::min(at+30,clean.size());const auto newline=clean.find('\n',at);
        if(newline!=std::string::npos && newline<end)end=newline;
        else if(end<clean.size()){const auto space=clean.rfind(' ',end);if(space!=std::string::npos && space>at+15)end=space;}
        append(clean.substr(at,end-at));at=end;
        if(at<clean.size() && (clean[at]=='\n' || clean[at]==' '))++at;
    }
    append("",true);
}
}
