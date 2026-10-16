#include "message_store.h"
#include <cstdlib>
#include <iostream>
#include <string>
static unsigned checks=0;
#define CHECK(...) do {++checks;if(!(__VA_ARGS__)){std::cerr<<"Store test failed at line "<<__LINE__<<": "<<#__VA_ARGS__<<'\n';std::exit(1);}}while(0)
int main() {
    fakeNvs::clear();
    MessageStore store;store.begin();
    CHECK(store.persistent && store.messages().empty());
    auto outgoing=store.add(Medium::Sms,false,"+15551234567","A pending SMS");
    CHECK(outgoing!=0 && store.messages().size()==1);
    MessageStore reboot;reboot.begin();
    CHECK(reboot.messages().size()==1);
    CHECK(reboot.messages()[0].delivery==Delivery::Unknown);
    CHECK(std::string(reboot.messages()[0].text)=="A pending SMS");
    reboot.update(outgoing,Delivery::Submitted);
    MessageStore reopened;reopened.begin();
    CHECK(reopened.messages()[0].delivery==Delivery::Submitted);
    auto incoming=reopened.add(Medium::Sms,true,"+15551234567","Received",123);
    CHECK(incoming!=0);
    CHECK(reopened.add(Medium::Sms,true,"+15551234567","Received",123)==0);
    CHECK(reopened.add(Medium::Radio,true,"Node 1234","Different transport",123)!=0);
    for(unsigned i=0;i<24;++i) reopened.add(Medium::Radio,true,"Node",std::to_string(i).c_str(),1000+i);
    CHECK(reopened.messages().size()==16);
    uint32_t last=reopened.messages().back().sequence;
    CHECK(std::string(reopened.messages().front().text)=="8");
    MessageStore retained;retained.begin();
    CHECK(retained.messages().size()==16 && retained.messages().back().sequence==last);
    CHECK(std::string(retained.messages().front().text)=="8");
    std::string key="deckos/m"+std::to_string(last%16);
    CHECK(fakeNvs::blobs.count(key)==1);
    fakeNvs::blobs[key][offsetof(Message,text)]^=1;
    MessageStore corrupt;corrupt.begin();
    CHECK(corrupt.messages().size()==15);
    for(const auto& message:corrupt.messages()) CHECK(message.sequence!=last);
    fakeNvs::failWrites=true;
    auto volatileMessage=corrupt.add(Medium::Sms,false,"+15551234567","RAM only");
    CHECK(!corrupt.persistent);
    CHECK(corrupt.messages().back().sequence==volatileMessage);
    CHECK(std::string(corrupt.messages().back().text)=="RAM only");
    fakeNvs::failWrites=false;
    MessageStore afterWriteFailure;afterWriteFailure.begin();
    for(const auto& message:afterWriteFailure.messages()) CHECK(std::string(message.text)!="RAM only");
    std::cout<<checks<<" message-store checks passed with a native NVS adapter\n";
}
