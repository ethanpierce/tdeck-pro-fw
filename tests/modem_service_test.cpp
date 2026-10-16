#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <at_engine.h>
#include <sms_pdu.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>
#include <atomic>
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <map>
// Exercise the actual UART-owner loop with fake hardware and a deterministic clock.
#include "../src/modem_task.cpp"
FakeSerial Serial1;
struct StopSimulation {};
uint32_t current=0,until=0;
std::function<void()> tickHook;
uint32_t millis(){return current;}
void vTaskDelay(TickType_t dt){current+=dt;if(tickHook)tickHook();if(current>=until)throw StopSimulation{};}
namespace board {void startModemPower(){}}
struct Harness {
    CellService service;
    std::vector<std::pair<uint32_t,std::string>> tx;
    std::map<std::string,unsigned> count;
    std::function<bool(const std::string&,unsigned)> custom;
    Harness(uint32_t stop) {
        current=0;until=stop;tickHook={};Serial1.rx.clear();
        if(cellEvents)vQueueDelete(cellEvents);
        if(cellActions)vQueueDelete(cellActions);
        cellEvents=xQueueCreate(4096,sizeof(CellEvent));cellActions=xQueueCreate(6,sizeof(CellAction));
        Serial1.onWrite=[this](std::string wire){
            tx.push_back({current,wire});
            std::string cmd=wire;if(!cmd.empty()&&cmd.back()=='\r')cmd.pop_back();
            unsigned n=++count[cmd];
            if(custom&&custom(cmd,n))return;
            if(cmd=="AT+CPIN?")Serial1.feed("\r\n+CPIN: READY\r\nOK\r\n");
            else if(cmd=="AT+CEREG?")Serial1.feed("\r\n+CEREG: 2,1\r\nOK\r\n");
            else if(cmd=="AT+CREG?")Serial1.feed("\r\n+CREG: 2,1\r\nOK\r\n");
            else if(cmd=="AT+CSQ")Serial1.feed("\r\n+CSQ: 20,99\r\nOK\r\n");
            else if(cmd.rfind("AT+CMGS=",0)==0)Serial1.feed("\r\n> ");
            else if(!wire.empty()&&wire.back()=='\x1a')Serial1.feed("\r\n+CMGS: 1\r\nOK\r\n");
            else if(cmd!="\x1b")Serial1.feed("\r\nOK\r\n");
        };
    }
    void run(){try{service.run();}catch(const StopSimulation&) {}}
    void action(CellActionKind kind,uint32_t id=0){CellAction a{};a.kind=kind;a.id=id;snprintf(a.peer,sizeof(a.peer),"+15551234567");snprintf(a.text,sizeof(a.text),"Hello");assert(xQueueSend(cellActions,&a,0));}
    void request(CellActionKind kind,const char* text){CellAction a{};a.kind=kind;snprintf(a.text,sizeof(a.text),"%s",text);assert(xQueueSend(cellActions,&a,0));}
    unsigned totalPrefix(std::string prefix){unsigned n=0;for(auto& item:tx)if(item.second.rfind(prefix,0)==0)++n;return n;}
    bool smsReady(){int ready=0;for(const auto& bytes:cellEvents->data){CellEvent e{};std::memcpy(&e,bytes.data(),sizeof(e));if(e.kind==CellEventKind::SmsReady)ready=e.value;}return ready!=0;}
    std::vector<CellEvent> events(){std::vector<CellEvent> out;CellEvent e{};while(xQueueReceive(cellEvents,&e,0))out.push_back(e);return out;}
};
int main(){
    {
        Harness h(62000);uint32_t readyAt=0;
        h.custom=[&](const std::string& cmd,unsigned n){
            if(cmd=="AT"&&n<3)return true;
            if(cmd=="AT+CPIN?"&&n==1){Serial1.feed("+CPIN: SIM PIN\r\nOK\r\n");return true;}
            if(cmd=="AT+CPIN?"&&n==2)readyAt=current;
            if(cmd.rfind("AT+CPMS=",0)==0&&n==1){Serial1.feed("+CMS ERROR: 310\r\n");return true;}
            return false;
        };
        h.run();assert(h.count["AT"]==3);assert(readyAt>15000);assert(h.count["AT+CMGF=0"]==2);
        for(auto& entry:h.tx)if(entry.second=="AT+CMGF=0\r")assert(entry.first>=readyAt);
        assert(h.smsReady());assert(h.count["AT+CMGL=4"]==1);assert(h.totalPrefix("ATD")==0);assert(h.totalPrefix("AT+CMGS=")==0);
        std::cout<<"PASS slow boot, late SIM, storage retry, no boot transmissions\n";
    }
    {
        Harness h(2000);
        h.custom=[&](const std::string& cmd,unsigned){return cmd=="AT+CPSI?";};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Dial);if(current==505)h.action(CellActionKind::Hangup);if(current==1000)Serial1.feed("OK\r\n");};
        h.run();assert(h.totalPrefix("ATD")==0);assert(h.count["AT+CHUP"]==1);
        std::cout<<"PASS hangup replaces unsent dial\n";
    }
    {
        Harness h(72000);
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty()&&cmd.back()=='\x1a';};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Sms,41);if(current==600)h.action(CellActionKind::Sms,42);};
        h.run();assert(h.totalPrefix("AT+CMGS=")==1);assert(h.count["AT"]==2);assert(h.smsReady());assert(h.count["AT+CMGL=4"]==2);
        bool unknown=false,rejected=false;
        for(const auto& e:h.events())if(e.kind==CellEventKind::Failed){if(e.id==41)unknown=e.value==1;if(e.id==42)rejected=e.value==0;}
        assert(unknown&&rejected);
        std::cout<<"PASS timeout unknown, one SMS at a time, reconnect without resend\n";
    }
    {
        Harness h(9000);
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty()&&cmd.back()=='\x1a';};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Sms,99);if(current==600)Serial1.feed("RDY\r\n");};
        h.run();assert(h.totalPrefix("AT+CMGS=")==1);assert(h.count["AT"]==2);assert(h.smsReady());
        bool unknown=false;for(const auto& e:h.events())if(e.kind==CellEventKind::Failed&&e.id==99)unknown=e.value==1;
        assert(unknown);std::cout<<"PASS modem restart mid-SMS marks unknown and reconfigures\n";
    }
    {
        Harness h(1000);h.custom=[&](const std::string& cmd,unsigned){
            if(cmd!="AT+CMGL=4")return false;
            std::string result;for(int i=0;i<100;++i){char time[32];snprintf(time,sizeof(time),"6210610000%02X00",i);result+="+CMGL: "+std::to_string(i+1)+",1,,20\r\n00000B915155214365F70000"+std::string(time)+"05E8329BFD06\r\n";}
            Serial1.feed(result+"OK\r\n");return true;
        };
        h.run();assert(h.smsReady());
        unsigned received=0;for(const auto& e:h.events())if(e.kind==CellEventKind::SmsIn)++received;
        assert(received==100);
        std::cout<<"PASS 100-message streaming inbox without response limit\n";
    }
    {
        Harness h(19000);uint32_t replyAt=0;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd!="AT+CLCC")return false;
            replyAt=current+3500;return true;
        };
        tickHook=[&]{if(current==replyAt)Serial1.feed("+CLCC: 1,0,0,0,0,\"15551234567\",145\r\nOK\r\n");};
        h.run();assert(h.smsReady());assert(h.count["AT+CPIN?"]>=2);
        std::cout<<"PASS slow call queries leave time for SIM diagnostics and SMS setup\n";
    }
    {
        Harness h(9000);bool applied=false;
        const std::string write="AT+CGDCONT=1,\"IPV4V6\",\"fast.t-mobile.com\"";
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CGDCONT?") {
                Serial1.feed(applied?"+CGDCONT: 1,\"IPV4V6\",\"fast.t-mobile.com\"\r\nOK\r\n":
                    "+CGDCONT: 1,\"IP\",\"old.apn\"\r\nOK\r\n");return true;
            }
            if(cmd==write) applied=true;
            return false;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::SetApn,"fast.t-mobile.com");if(current==5000)h.action(CellActionKind::Refresh);};
        h.run();assert(applied && h.count[write]==1 && h.count["AT+CGDCONT?"]==2);
        bool notified=false;for(const auto& e:h.events())if(e.kind==CellEventKind::ApnResult&&e.value)notified=true;
        assert(notified);
        std::cout<<"PASS APN apply on UART owner, reconnect checks existing profile without rewriting\n";
    }
    {
        Harness h(2000);
        tickHook=[&]{if(current==500)h.request(CellActionKind::SetApn,"bad\";ATD123;");if(current==600)h.request(CellActionKind::NetworkQuery,"AT+CFUN=0");};
        h.run();assert(h.totalPrefix("AT+CGDCONT=")==0 && h.count["AT+CFUN=0"]==0);
        std::cout<<"PASS invalid APN and non-query modem commands rejected\n";
    }
    {
        Harness h(125000);uint32_t replyAt=0;
        h.custom=[&](const std::string& cmd,unsigned){if(cmd!="AT+COPS=?")return false;replyAt=current+90000;return true;};
        tickHook=[&]{
            if(current==1000)h.request(CellActionKind::NetworkQuery,"AT+COPS=?");
            if(replyAt && current==replyAt)Serial1.feed("+COPS: (1,\"Test LTE\",\"Test\",\"310260\",7)\r\nOK\r\n");
        };
        h.run();assert(h.count["AT+COPS=?"]==1 && h.count["AT"]==1);
        bool completed=false;for(const auto& e:h.events())if(e.kind==CellEventKind::Trace&&std::string(e.text)=="AT+COPS=?: OK")completed=true;
        assert(completed);
        std::cout<<"PASS 90-second network scan completes without short query timeout or reconnect\n";
    }
    {
        Harness h(75000);uint32_t replyAt=0;
        h.custom=[&](const std::string& cmd,unsigned){if(cmd!="AT+COPS=?")return false;replyAt=current+60000;return true;};
        tickHook=[&]{
            if(current==1000)h.action(CellActionKind::CarrierScan);
            if(replyAt && current==replyAt)Serial1.feed("+COPS: (1,\"Test LTE\",\"Test\",\"310260\",7)\r\nOK\r\n");
        };
        h.run();bool started=false,entry=false,done=false;
        for(const auto& e:h.events()) {
            if(e.kind==CellEventKind::CarrierStart)started=true;
            if(e.kind==CellEventKind::Carrier)entry=e.value==1 && e.id==7 && std::string(e.peer)=="310260";
            if(e.kind==CellEventKind::CarrierDone)done=e.value==1;
        }
        assert(started && entry && done && h.count["AT+COPS=?"]==1 && h.count["AT"]==1);
        std::cout<<"PASS on-device carrier scan reports start, parsed result, completion without reconnect\n";
    }
    {
        Harness h(20000);bool applied=false;
        const std::string write="AT+CGDCONT=1,\"IPV4V6\",\"NXTGENPHONE\"";
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CGDCONT?") {
                Serial1.feed(applied?"+CGDCONT: 1,\"IPV4V6\",\"NXTGENPHONE\"\r\nOK\r\n":
                    "+CGDCONT: 1,\"IP\",\"old.apn\"\r\nOK\r\n");return true;
            }
            if(cmd==write)applied=true;
            return false;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::SetApn,"NXTGENPHONE");if(current==1000)h.action(CellActionKind::Restart);};
        h.run();assert(h.count["AT+CFUN=1,1"]==1 && h.count["AT"]==2);
        assert(h.smsReady() && applied && h.count[write]==1 && h.count["AT+CGDCONT?"]==2);
        assert(h.totalPrefix("AT+CMGS=")==0 && h.totalPrefix("ATD")==0);
        std::cout<<"PASS explicit modem restart reinitializes SIM and retains APN without sending messages\n";
    }
    {
        Harness h(2000);
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty()&&cmd.back()=='\x1a';};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Sms,77);if(current==600)h.action(CellActionKind::Restart);};
        h.run();assert(h.totalPrefix("AT+CMGS=")==1 && h.count["AT+CFUN=1,1"]==0);
        bool refused=false;for(const auto& e:h.events())if(e.kind==CellEventKind::Trace&&std::string(e.text).find("Modem busy or offline; restart")!=std::string::npos)refused=true;
        assert(refused);std::cout<<"PASS restart refuses to interrupt an SMS in progress\n";
    }
    {
        Harness h(27000);
        h.custom=[&](const std::string& cmd,unsigned){return cmd=="AT+CFUN=1,1";};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Restart);};
        h.run();assert(h.count["AT+CFUN=1,1"]==1 && h.count["AT"]==2 && h.smsReady());
        assert(h.totalPrefix("AT+CMGS=")==0 && h.totalPrefix("ATD")==0);
        std::cout<<"PASS uncertain restart recovers the UART without automatically repeating the reset\n";
    }
    {
        Harness h(95000);uint32_t replyAt=0;
        h.custom=[&](const std::string& cmd,unsigned){if(cmd!="AT+COPS=0")return false;replyAt=current+90000;return true;};
        tickHook=[&]{
            if(current==1000)h.action(CellActionKind::SelectAutomatic);
            if(replyAt && current==replyAt)Serial1.feed("OK\r\n");
        };
        h.run();assert(h.count["AT+COPS=0"]==1 && h.count["AT"]==1 && h.count["AT+CGMM"]==2);
        assert(h.totalPrefix("AT+CMGS=")==0 && h.totalPrefix("ATD")==0);
        bool completed=false;for(const auto& e:h.events())if(e.kind==CellEventKind::Trace&&std::string(e.text)=="Automatic network selection: OK")completed=true;
        assert(completed);std::cout<<"PASS slow automatic carrier selection completes and refreshes diagnostics\n";
    }
    {
        Harness h(2000);std::string store="SM";unsigned smScans=0,meScans=0;
        const std::string pdu="00000B915155214365F700006210610000000005E8329BFD06";
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"") {
                store="ME";
                // A SIM notification must not read slot 7 from the temporarily selected ME store.
                Serial1.feed("+CPMS: 1,100,0,100,0,100\r\nOK\r\n+CMTI: \"SM\",7\r\n");return true;
            }
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd.rfind("AT+CMGR=",0)==0)assert(store=="SM");
            if(cmd!="AT+CMGL=4")return false;
            if(store=="SM") {++smScans;Serial1.feed("OK\r\n");}
            else {++meScans;Serial1.feed("+CMGL: 7,0,,20\r\n"+pdu+"\r\nOK\r\n");}
            return true;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::NetworkQuery,"inbox-me");};
        h.run();assert(store=="SM" && h.smsReady() && smScans==2 && meScans==1);
        assert(h.count["AT+CPMS=\"ME\""]==1 && h.count["AT+CPMS=\"SM\""]==1);
        unsigned received=0;for(const auto& e:h.events())if(e.kind==CellEventKind::SmsIn) {
            ++received;assert(std::string(e.text)=="hello");
        }
        assert(received==1 && h.totalPrefix("AT+CMGD")==0 && h.totalPrefix("AT+CMGS=")==0);
        std::cout<<"PASS explicit ME inbox imports a message, defers interleaved SIM notification and restores SM without deleting or sending\n";
    }
    {
        Harness h(2000);
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd!="AT+CPMS=\"ME\"")return false;
            Serial1.feed("+CMS ERROR: 302\r\n");return true;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::NetworkQuery,"inbox-me");};
        h.run();assert(h.smsReady() && h.count["AT+CPMS=\"SM\""]==1 && h.count["AT+CMGL=4"]==1);
        std::cout<<"PASS unavailable ME storage restores SM and does not scan the wrong inbox\n";
    }
    {
        Harness h(2500);std::string store="SM";unsigned meScans=0;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\"") {
                Serial1.feed("+CMS ERROR: 310\r\n+CMTI: \"SM\",4\r\n");return true;
            }
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"SM\"")store="SM";
            if(cmd=="AT+CMGL=4" && store=="ME")++meScans;
            if(cmd.rfind("AT+CMGR=",0)==0)assert(store=="SM");
            return false;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::NetworkQuery,"inbox-me");};
        h.run();assert(store=="SM" && h.smsReady() && meScans==1);
        assert(h.count["AT+CPMS=\"SM\",\"SM\",\"SM\""]==2 && h.count["AT+CMGL=4"]==3);
        std::cout<<"PASS failed SM restore blocks slot reads until full SMS setup repairs the storage selection\n";
    }
    {
        Harness h(41000);std::string store="SM";unsigned meScans=0;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"SM\"")store="SM";
            if(cmd=="AT+CMGL=4" && store=="ME") {++meScans;return true;}
            return false;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::NetworkQuery,"inbox-me");};
        h.run();assert(store=="SM" && h.smsReady() && meScans==1 && h.count["AT"]==2);
        assert(h.totalPrefix("AT+CMGS=")==0 && h.totalPrefix("AT+CMGD")==0 && h.totalPrefix("ATD")==0);
        std::cout<<"PASS ME inbox timeout reconnects and restores SIM storage without repeating the probe or sending anything\n";
    }
    {
        Harness h(2000);
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty()&&cmd.back()=='\x1a';};
        tickHook=[&]{
            if(current==500)h.action(CellActionKind::Sms,81);
            if(current==600)h.request(CellActionKind::NetworkQuery,"inbox-me");
        };
        h.run();assert(h.totalPrefix("AT+CMGS=")==1 && h.count["AT+CPMS=\"ME\""]==0);
        bool refused=false;for(const auto& e:h.events())if(e.kind==CellEventKind::Trace && std::string(e.text).find("retry inbox-me shortly")!=std::string::npos)refused=true;
        assert(refused);std::cout<<"PASS ME inbox inspection refuses to interrupt an SMS\n";
    }
    vQueueDelete(cellEvents);vQueueDelete(cellActions);cellEvents=cellActions=nullptr;
}
