#include "message_store.h"
#include <sms_pdu.h>
#include <algorithm>
#include <cstring>
namespace {
uint32_t checksum(Message message) {
    message.checksum=0;
    constexpr size_t encodedSize=offsetof(Message,text)+sizeof(message.text);
    return deck::fingerprint(std::string(reinterpret_cast<const char*>(&message),encodedSize));
}
}
void MessageStore::begin() {
    persistent=prefs_.begin("deckos",false);
    if (!persistent) return;
    for (unsigned i=0;i<16;++i) {
        char key[8]; snprintf(key,sizeof(key),"m%u",i);
        Message message{};
        if (prefs_.getBytesLength(key)!=sizeof(message) || prefs_.getBytes(key,&message,sizeof(message))!=sizeof(message)) continue;
        if (message.magic!=0x444b0001 || message.checksum!=checksum(message) ||
            message.peer[31] || message.text[480] || uint8_t(message.medium)>1 || uint8_t(message.delivery)>4) continue;
        next_=std::max(next_,message.sequence+1);
        messages_.push_back(message);
    }
    std::sort(messages_.begin(),messages_.end(),[](const Message& a,const Message& b){return a.sequence<b.sequence;});
    // A reboot during transmission cannot establish whether the remote side received it.
    for (auto& message:messages_) if (message.delivery==Delivery::Pending) {
        message.delivery=Delivery::Unknown; save(message);
    }
}
bool MessageStore::save(Message& message) {
    message.checksum=checksum(message);
    if (!persistent) return false;
    char key[8]; snprintf(key,sizeof(key),"m%u",unsigned(message.sequence%16));
    if (prefs_.putBytes(key,&message,sizeof(message))!=sizeof(message)) {persistent=false; return false;}
    return true;
}
uint32_t MessageStore::add(Medium medium,bool incoming,const char* peer,const char* text,uint32_t reference) {
    if (incoming && reference)
        for (const auto& old:messages_) if(old.incoming && old.medium==medium && old.reference==reference) return 0;
    Message message{}; message.sequence=next_++; message.reference=reference;
    message.medium=medium; message.incoming=incoming;
    message.delivery=incoming?Delivery::Received:Delivery::Pending;
    snprintf(message.peer,sizeof(message.peer),"%s",peer);
    snprintf(message.text,sizeof(message.text),"%s",text);
    save(message);
    if (messages_.size()==16) messages_.erase(messages_.begin());
    messages_.push_back(message);
    return message.sequence;
}
void MessageStore::update(uint32_t sequence,Delivery delivery) {
    for (auto& message:messages_) if(message.sequence==sequence) {message.delivery=delivery; save(message); break;}
}
