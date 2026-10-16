#include "conversations.h"
#include <algorithm>
#include <array>
#include <utility>

namespace deck {
namespace {
std::string preview(std::string_view text) {
    std::string result;
    result.reserve(std::min(text.size(),ConversationPreviewLimit));
    for(size_t i=0;i<text.size() && result.size()<ConversationPreviewLimit;) {
        const auto c=static_cast<unsigned char>(text[i]);
        size_t unit=c<0x80?1:c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0;
        bool valid=unit && unit<=text.size()-i;
        for(size_t j=1;valid && j<unit;++j)
            valid=(static_cast<unsigned char>(text[i+j])&0xc0)==0x80;
        if(valid && unit>=3) {
            const auto second=static_cast<unsigned char>(text[i+1]);
            if((c==0xe0 && second<0xa0) || (c==0xed && second>=0xa0) ||
               (c==0xf0 && second<0x90) || (c==0xf4 && second>=0x90)) valid=false;
        }
        if(!valid) {result+='?';++i;continue;}
        if(result.size()+unit>ConversationPreviewLimit) break;
        if(unit==1 && (c<0x20 || c==0x7f)) result+=' ';
        else result.append(text.data()+i,unit);
        i+=unit;
    }
    return result;
}
}
std::string conversationPeerKey(std::string_view peer) {
    if(peer.empty() || peer.size()>16) return {};
    if(peer.front()=='+') peer.remove_prefix(1);
    if(peer.size()<3 || peer.size()>15) return {};
    for(const char c:peer) if(c<'0' || c>'9') return {};
    return std::string(peer);
}
bool conversationMatches(const Conversation& conversation,const ConversationMessage& message) {
    if(conversation.kind==ConversationKind::NearbyLoRa) return message.radio;
    if(conversation.kind!=ConversationKind::Sms || message.radio) return false;
    const auto key=conversationPeerKey(message.peer);
    return !key.empty() && key==conversation.key;
}
std::vector<Conversation> conversationIndex(const std::vector<ConversationMessage>& messages) {
    std::array<const ConversationMessage*,ConversationMessageLimit> newest{};
    size_t count=0;
    for(const auto& message:messages) {
        if(!message.radio && conversationPeerKey(message.peer).empty()) continue;
        size_t position=0;
        while(position<count && newest[position]->sequence>message.sequence) ++position;
        if(position==ConversationMessageLimit) continue;
        const size_t end=std::min(count,ConversationMessageLimit-1);
        for(size_t i=end;i>position;--i) newest[i]=newest[i-1];
        newest[position]=&message;
        count=std::min(count+1,ConversationMessageLimit);
    }
    std::vector<Conversation> result;
    result.reserve(count);
    for(size_t i=0;i<count;++i) {
        const auto& message=*newest[i];
        const auto kind=message.radio?ConversationKind::NearbyLoRa:ConversationKind::Sms;
        const std::string key=message.radio?"lora:broadcast":conversationPeerKey(message.peer);
        const auto found=std::find_if(result.begin(),result.end(),[&](const Conversation& item){
            return item.kind==kind && item.key==key;
        });
        if(found!=result.end()) {++found->messageCount;continue;}
        Conversation item;
        item.kind=kind;item.key=key;item.latestSequence=message.sequence;item.messageCount=1;
        if(!message.radio) item.replyAddress=std::string(message.peer);
        item.preview=preview(message.text);
        result.push_back(std::move(item));
    }
    return result;
}
}
