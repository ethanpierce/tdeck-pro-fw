#include <platform.h>
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
#ifdef DECK_CELLULAR_SOCKET_TEST
#include "../src/cellular_socket.h"
#endif
// Exercise the actual UART-owner loop with fake hardware and a deterministic clock.
#include "../src/modem_task.cpp"
platform::FakeUart platform::modemUart;
struct StopSimulation {};
uint32_t current=0,until=0;
std::function<void()> tickHook;
uint32_t platform::millis(){return current;}
void vTaskDelay(TickType_t dt){current+=dt;if(tickHook)tickHook();if(current>=until)throw StopSimulation{};}
unsigned modemStarts=0,modemStops=0;
namespace board {
bool startModemPower(bool (*allowed)()) {if(allowed && !allowed())return false;++modemStarts;return true;}
void stopModemPower(){++modemStops;}
}
struct Harness {
    CellService service;
    std::vector<std::pair<uint32_t,std::string>> tx;
    std::map<std::string,unsigned> count;
    std::function<bool(const std::string&,unsigned)> custom;
    Harness(uint32_t stop) {
        current=0;until=stop;tickHook={};platform::modemUart.rx.clear();
        cellSuspendForAirplane(false);modemStarts=modemStops=0;
        if(cellEvents)vQueueDelete(cellEvents);
        if(cellActions)vQueueDelete(cellActions);
        cellEvents=xQueueCreate(4096,sizeof(CellEvent));cellActions=xQueueCreate(6,sizeof(CellAction));
        platform::modemUart.onWrite=[this](std::string wire){
            tx.push_back({current,wire});
            std::string cmd=wire;if(!cmd.empty()&&cmd.back()=='\r')cmd.pop_back();
            unsigned n=++count[cmd];
            if(custom&&custom(cmd,n))return;
            if(cmd=="AT+CPIN?")platform::modemUart.feed("\r\n+CPIN: READY\r\nOK\r\n");
            else if(cmd=="AT+CEREG?")platform::modemUart.feed("\r\n+CEREG: 2,1\r\nOK\r\n");
            else if(cmd=="AT+CREG?")platform::modemUart.feed("\r\n+CREG: 2,1\r\nOK\r\n");
            else if(cmd=="AT+CSQ")platform::modemUart.feed("\r\n+CSQ: 20,99\r\nOK\r\n");
            else if(cmd=="AT+CIPMODE?")platform::modemUart.feed("\r\n+CIPMODE: 0\r\nOK\r\n");
            else if(cmd.rfind("AT+CMGS=",0)==0)platform::modemUart.feed("\r\n> ");
            else if(!wire.empty()&&wire.back()=='\x1a')platform::modemUart.feed("\r\n+CMGS: 1\r\nOK\r\n");
            else if(cmd!="\x1b")platform::modemUart.feed("\r\nOK\r\n");
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
        Harness h(2000);cellSuspendForAirplane(true);
        tickHook=[&]{if(current==100)h.action(CellActionKind::Sms,711);if(current==200)h.action(CellActionKind::Dial);};
        h.run();assert(h.tx.empty() && modemStarts==0 && modemStops>0 && cellAirplaneStopped() && !cellTcpReady());
        bool rejected=false;for(const auto& event:h.events())if(event.kind==CellEventKind::Failed && event.id==711)rejected=event.value==0;
        assert(rejected);std::cout<<"PASS persisted Airplane mode never powers modem or transmits queued SMS/call\n";
    }
    {
        Harness h(6000);
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty() && cmd.back()=='\x1a';};
        tickHook=[&]{
            if(current==500)h.action(CellActionKind::Sms,712);
            if(current==600){cellSuspendForAirplane(true);h.action(CellActionKind::Sms,713);h.action(CellActionKind::Dial);}
            if(current==1600)cellSuspendForAirplane(false);
        };
        h.run();assert(h.totalPrefix("AT+CMGS=")==1 && h.totalPrefix("ATD")==0 && modemStarts==2);
        for(const auto& item:h.tx)assert(item.first<600 || item.first>=1600);
        const bool restored=h.smsReady();
        bool unknown=false,rejected=false;for(const auto& event:h.events())if(event.kind==CellEventKind::Failed) {
            if(event.id==712)unknown=event.value==1;
            if(event.id==713)rejected=event.value==0;
        }
        assert(unknown && rejected && restored);
        std::cout<<"PASS Airplane mode cancels in-flight SMS as unknown, drops queued sends, restores without replay\n";
    }
    {
        Harness h(6000);
        tickHook=[&]{if(current==500){h.action(CellActionKind::Dial);cellSuspendForAirplane(true);cellSuspendForAirplane(false);}};
        h.run();assert(modemStarts==2 && modemStops>0 && h.totalPrefix("ATD")==0 && h.smsReady());
        std::cout<<"PASS rapid Airplane on/off still power-cycles and discards an unsent dial\n";
    }
    {
        Harness h(62000);uint32_t readyAt=0;
        h.custom=[&](const std::string& cmd,unsigned n){
            if(cmd=="AT"&&n<3)return true;
            if(cmd=="AT+CPIN?"&&n==1){platform::modemUart.feed("+CPIN: SIM PIN\r\nOK\r\n");return true;}
            if(cmd=="AT+CPIN?"&&n==2)readyAt=current;
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"ME\""&&n==1){platform::modemUart.feed("+CMS ERROR: 310\r\n");return true;}
            return false;
        };
        h.run();assert(h.count["AT"]==3);assert(readyAt>15000);assert(h.count["AT+CMGF=0"]==2);
        for(auto& entry:h.tx)if(entry.second=="AT+CMGF=0\r")assert(entry.first>=readyAt);
        assert(h.smsReady());assert(h.count["AT+CMGL=4"]==2);assert(h.totalPrefix("ATD")==0);assert(h.totalPrefix("AT+CMGS=")==0);
        std::cout<<"PASS slow boot, late SIM, storage retry, no boot transmissions\n";
    }
    {
        Harness h(2000);
        h.custom=[&](const std::string& cmd,unsigned){return cmd=="AT+CPSI?";};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Dial);if(current==505)h.action(CellActionKind::Hangup);if(current==1000)platform::modemUart.feed("OK\r\n");};
        h.run();assert(h.totalPrefix("ATD")==0);assert(h.count["AT+CHUP"]==1);
        std::cout<<"PASS hangup replaces unsent dial\n";
    }
    {
        Harness h(72000);
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty()&&cmd.back()=='\x1a';};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Sms,41);if(current==600)h.action(CellActionKind::Sms,42);};
        h.run();assert(h.totalPrefix("AT+CMGS=")==1);assert(h.count["AT"]==2);assert(h.smsReady());assert(h.count["AT+CMGL=4"]==4);
        bool unknown=false,rejected=false;
        for(const auto& e:h.events())if(e.kind==CellEventKind::Failed){if(e.id==41)unknown=e.value==1;if(e.id==42)rejected=e.value==0;}
        assert(unknown&&rejected);
        std::cout<<"PASS timeout unknown, one SMS at a time, reconnect without resend\n";
    }
    {
        Harness h(9000);
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty()&&cmd.back()=='\x1a';};
        tickHook=[&]{if(current==500)h.action(CellActionKind::Sms,99);if(current==600)platform::modemUart.feed("RDY\r\n");};
        h.run();assert(h.totalPrefix("AT+CMGS=")==1);assert(h.count["AT"]==2);assert(h.smsReady());
        bool unknown=false;for(const auto& e:h.events())if(e.kind==CellEventKind::Failed&&e.id==99)unknown=e.value==1;
        assert(unknown);std::cout<<"PASS modem restart mid-SMS marks unknown and reconfigures\n";
    }
    {
        Harness h(1000);h.custom=[&](const std::string& cmd,unsigned){
            if(cmd!="AT+CMGL=4")return false;
            std::string result;for(int i=0;i<100;++i){char time[32];snprintf(time,sizeof(time),"6210610000%02X00",i);result+="+CMGL: "+std::to_string(i+1)+",1,,20\r\n00000B915155214365F70000"+std::string(time)+"05E8329BFD06\r\n";}
            platform::modemUart.feed(result+"OK\r\n");return true;
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
        tickHook=[&]{if(current==replyAt)platform::modemUart.feed("+CLCC: 1,0,0,0,0,\"15551234567\",145\r\nOK\r\n");};
        h.run();assert(h.smsReady());assert(h.count["AT+CPIN?"]>=2);
        std::cout<<"PASS slow call queries leave time for SIM diagnostics and SMS setup\n";
    }
    {
        Harness h(9000);bool applied=false;
        const std::string write="AT+CGDCONT=1,\"IPV4V6\",\"fast.t-mobile.com\"";
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CGDCONT?") {
                platform::modemUart.feed(applied?"+CGDCONT: 1,\"IPV4V6\",\"fast.t-mobile.com\"\r\nOK\r\n":
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
            if(replyAt && current==replyAt)platform::modemUart.feed("+COPS: (1,\"Test LTE\",\"Test\",\"310260\",7)\r\nOK\r\n");
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
            if(replyAt && current==replyAt)platform::modemUart.feed("+COPS: (1,\"Test LTE\",\"Test\",\"310260\",7)\r\nOK\r\n");
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
                platform::modemUart.feed(applied?"+CGDCONT: 1,\"IPV4V6\",\"NXTGENPHONE\"\r\nOK\r\n":
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
            if(replyAt && current==replyAt)platform::modemUart.feed("OK\r\n");
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
                platform::modemUart.feed("+CPMS: 1,100,0,100,0,100\r\nOK\r\n+CMTI: \"SM\",7\r\n");return true;
            }
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd.rfind("AT+CMGR=",0)==0)assert(store=="SM");
            if(cmd!="AT+CMGL=4")return false;
            if(store=="SM") {++smScans;platform::modemUart.feed("OK\r\n");}
            else {++meScans;platform::modemUart.feed("+CMGL: 7,0,,20\r\n"+pdu+"\r\nOK\r\n");}
            return true;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::NetworkQuery,"inbox-me");};
        h.run();assert(store=="SM" && h.smsReady() && smScans==3 && meScans==2);
        assert(h.count["AT+CPMS=\"ME\""]==2 && h.count["AT+CPMS=\"SM\""]==2);
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
            platform::modemUart.feed("+CMS ERROR: 302\r\n");return true;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::NetworkQuery,"inbox-me");};
        h.run();assert(h.smsReady() && h.count["AT+CPMS=\"SM\""]==2 && h.count["AT+CMGL=4"]==1);
        std::cout<<"PASS unavailable ME storage restores SM and does not scan the wrong inbox\n";
    }
    {
        Harness h(2500);std::string store="SM";unsigned meScans=0;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\"") {
                platform::modemUart.feed("+CMS ERROR: 310\r\n+CMTI: \"SM\",4\r\n");return true;
            }
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"SM\"" || cmd=="AT+CPMS=\"SM\",\"SM\",\"ME\"")store="SM";
            if(cmd=="AT+CMGL=4" && store=="ME")++meScans;
            if(cmd.rfind("AT+CMGR=",0)==0)assert(store=="SM");
            return false;
        };
        tickHook=[&]{if(current==500)h.request(CellActionKind::NetworkQuery,"inbox-me");};
        h.run();assert(store=="SM" && h.smsReady() && meScans==2);
        assert(h.count["AT+CPMS=\"SM\",\"SM\",\"ME\""]==3 && h.count["AT+CMGL=4"]==5);
        std::cout<<"PASS failed SM restore blocks slot reads until full SMS setup repairs the storage selection\n";
    }
    {
        Harness h(41000);std::string store="SM";unsigned meScans=0;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"SM\"" || cmd=="AT+CPMS=\"SM\",\"SM\",\"ME\"")store="SM";
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
        h.run();assert(h.totalPrefix("AT+CMGS=")==1 && h.count["AT+CPMS=\"ME\""]==1);
        bool refused=false;for(const auto& e:h.events())if(e.kind==CellEventKind::Trace && std::string(e.text).find("retry inbox-me shortly")!=std::string::npos)refused=true;
        assert(refused);std::cout<<"PASS ME inbox inspection refuses to interrupt an SMS\n";
    }
    {
        Harness h(2000);std::string store="SM",receive="SM";unsigned smScans=0,meScans=0;
        bool second=false;
        const std::string first="00000B915155214365F700006210610000000005E8329BFD06";
        const std::string later="00000B915155214365F700006210610000010005E8329BFD06";
        const std::string carrier="00400B915155214365F70000621061000000000B050003CC0201E8329BFD06";
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"ME\"") {
                store="SM";receive="ME";platform::modemUart.feed("+CPMS: 5,5,5,5,1,180\r\nOK\r\n");return true;
            }
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd!="AT+CMGL=4")return false;
            std::string result;
            if(store=="SM") {
                ++smScans;
                for(unsigned i=1;i<=5;++i)result+="+CMGL: "+std::to_string(i)+",1,,25\r\n"+carrier+"\r\n";
            } else {
                ++meScans;result="+CMGL: 1,0,,20\r\n"+first+"\r\n";
                if(second)result+="+CMGL: 2,0,,20\r\n"+later+"\r\n";
            }
            platform::modemUart.feed(result+"OK\r\n");return true;
        };
        tickHook=[&]{
            if(current==500){second=true;platform::modemUart.feed("+CMTI: \"ME\",2\r\n");}
            if(current==1000)h.action(CellActionKind::ReadInbox);
        };
        h.run();assert(h.smsReady() && store=="SM" && receive=="ME" && smScans==2 && meScans==3);
        unsigned received=0;
        for(const auto& e:h.events()) {
            if(e.kind==CellEventKind::SmsIn)++received;
            if(e.kind==CellEventKind::Trace) {
                const std::string text=e.text;
                assert(text.find(first)==std::string::npos && text.find(carrier)==std::string::npos && text.rfind("+CMGL:",0)!=0);
            }
        }
        assert(received==2 && h.totalPrefix("AT+CMGD")==0 && h.totalPrefix("AT+CMGS=")==0 && h.count["AT+CFUN=1,1"]==0);
        std::cout<<"PASS full SIM is preserved while ME receives/imports, both stores rescan on request, duplicates suppressed and automatic PDUs stay private\n";
    }
    {
        Harness h(2000);std::string store="SM";unsigned received=0;
        const std::string first="00000B915155214365F700006210610000000005E8329BFD06";
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"ME\"" || cmd=="AT+CPMS=\"ME\"") {
                platform::modemUart.feed("+CMS ERROR: 302\r\n");return true;
            }
            if(cmd=="AT+CPMS=\"SM\",\"SM\",\"SM\"") {
                store="SM";platform::modemUart.feed("+CPMS: 5,5,5,5,5,5\r\nOK\r\n");return true;
            }
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd.rfind("AT+CMGR=",0)==0) {
                assert(store=="SM");platform::modemUart.feed("+CMGR: 0,,20\r\n"+first+"\r\nOK\r\n");return true;
            }
            return false;
        };
        tickHook=[&]{if(current==500)platform::modemUart.feed("+CMTI: \"SM\",3\r\n");};
        h.run();assert(h.smsReady() && h.count["AT+CPMS=\"SM\",\"SM\",\"SM\""]==1 && h.count["AT+CPMS=\"ME\""]==1);
        bool warned=false;
        for(const auto& e:h.events()) {
            if(e.kind==CellEventKind::SmsIn)++received;
            if(e.kind==CellEventKind::Trace && std::string(e.text).find("Incoming SMS storage is full")!=std::string::npos)warned=true;
        }
        assert(received==1 && warned && h.totalPrefix("AT+CMGD")==0);
        std::cout<<"PASS unsupported ME safely falls back to SIM, warns that the receive store is full and still reads SIM notifications\n";
    }
    {
        Harness h(2500);std::string store="SM";unsigned smScans=0,meScans=0;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd.rfind("AT+CMGR=",0)==0)assert(store=="SM");
            if(cmd!="AT+CMGL=4")return false;
            if(store=="SM") {++smScans;platform::modemUart.feed("OK\r\n");return true;}
            ++meScans;std::string result;
            if(meScans==1) {
                result="+CMTI: \"SM\",3\r\n+CMTI: \"ME\",2\r\n";
                h.action(CellActionKind::ReadInbox); // Request during temporary ME selection is retained.
            }
            for(int i=0;i<100;++i) {
                char time[32];snprintf(time,sizeof(time),"6210610000%02X00",i);
                result+="+CMGL: "+std::to_string(i+1)+",1,,20\r\n00000B915155214365F70000"+std::string(time)+"05E8329BFD06\r\n";
            }
            platform::modemUart.feed(result+"OK\r\n");return true;
        };
        h.run();assert(h.smsReady() && store=="SM" && smScans==2 && meScans==2);
        unsigned received=0;for(const auto& e:h.events())if(e.kind==CellEventKind::SmsIn)++received;
        assert(received==100 && h.totalPrefix("AT+CMGD")==0 && h.totalPrefix("AT+CMGS=")==0);
        std::cout<<"PASS 100-message ME stream handles interleaved SM/ME indications and a concurrent import request without duplicate delivery\n";
    }
    {
        Harness h(19000);std::string store="SM";unsigned meScans=0;
        const auto droppedBefore=droppedCellEvents.load();
        vQueueDelete(cellEvents);cellEvents=xQueueCreate(1,sizeof(CellEvent));
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd=="AT+CMGL=4" && store=="ME") {
                ++meScans;platform::modemUart.feed("+CMGL: 1,0,,20\r\n00000B915155214365F700006210610000000005E8329BFD06\r\nOK\r\n");return true;
            }
            return false;
        };
        tickHook=[&]{if(current==500){vQueueDelete(cellEvents);cellEvents=xQueueCreate(4096,sizeof(CellEvent));}};
        h.run();const bool ready=h.smsReady();unsigned received=0;
        for(const auto& e:h.events())if(e.kind==CellEventKind::SmsIn)++received;
        assert(ready && received==1 && meScans==2 && droppedCellEvents.load()>droppedBefore);
        assert(h.totalPrefix("AT+CMGD")==0 && h.totalPrefix("AT+CMGS=")==0 && h.count["AT"]==1);
        std::cout<<"PASS full UI queue leaves ME message pending for bounded retry and does not send, delete or reset\n";
    }
#ifdef DECK_CELLULAR_SOCKET_TEST
    {
        Harness h(2500);std::string store="SM";bool incoming=false;
        const std::string ciphertext=std::string("\x16\x03\x03\0\x07",5)+"TLS\r\n>A";
        const std::string payload="\r\nOK\r\n+CMTI: \"SM\",88\r\n+CEREG: 2,3\r\n";
        std::string hex;const char* digits="0123456789ABCDEF";
        for(unsigned char c:payload){hex+=digits[c>>4];hex+=digits[c&15];}
        void* request=nullptr;unsigned stage=0;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd=="AT+NETOPEN?"){platform::modemUart.feed("+NETOPEN: 1\r\nOK\r\n");return true;}
            if(cmd.rfind("AT+CIPOPEN=9,",0)==0) {
                platform::modemUart.feed("+CIPOPEN: 8,0\r\n+CIPOPEN: 9,0\r\nOK\r\n");return true;
            }
            if(cmd.rfind("AT+CIPSEND=9,",0)==0){platform::modemUart.feed("> ");return true;}
            if(cmd==ciphertext) {
                platform::modemUart.feed("+CIPSEND: 9,"+std::to_string(ciphertext.size())+","+std::to_string(ciphertext.size())+"\r\nOK\r\n");return true;
            }
            if(cmd=="AT+CIPRXGET=3,9,256") {
                incoming=true;
                platform::modemUart.feed("+CMTI: \"SM\",3\r\n+CIPRXGET: 3,9,"+std::to_string(payload.size())+",0\r\n"+hex+"\r\nOK\r\n");return true;
            }
            if(cmd=="AT+CIPCLOSE=9"){platform::modemUart.feed("OK\r\n+CIPCLOSE: 9,0\r\n");return true;}
            if(cmd=="AT+CMGL=4" && store=="SM" && incoming) {
                platform::modemUart.feed("+CMGL: 3,0,,20\r\n00000B915155214365F700006210610000000005E8329BFD06\r\nOK\r\n");return true;
            }
            return false;
        };
        tickHook=[&]{
            if(current==500){assert(cellTcpReady());request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test");stage=1;assert(request);}
            int result=0;std::string data;int64_t timestamp=0;
            if(!request || !cellTcpTestTake(request,result,data,timestamp))return;
            request=nullptr;
            if(stage==1){assert(result==1);request=cellTcpTestSubmit(CellTcpTestKind::Write,ciphertext);stage=2;}
            else if(stage==2){assert(result==int(ciphertext.size()));request=cellTcpTestSubmit(CellTcpTestKind::Read,"",256);stage=3;}
            else if(stage==3){assert(result==int(payload.size()) && data==payload);request=cellTcpTestSubmit(CellTcpTestKind::Close);stage=4;}
            else {assert(stage==4 && result==1);stage=5;}
        };
        h.run();assert(stage==5 && !request && h.smsReady() && store=="SM");
        bool exactWrite=false;for(const auto& entry:h.tx)if(entry.second==ciphertext)exactWrite=true;
        assert(exactWrite && h.count[ciphertext+"\x1a"]==0 && h.count["AT+CMGR=88"]==0 && h.totalPrefix("AT+CMGS=")==0);
        unsigned received=0;
        for(const auto& e:h.events()) {
            if(e.kind==CellEventKind::SmsIn)++received;
            if(e.kind==CellEventKind::Registration)assert(e.value!=3);
        }
        assert(received==1);
        std::cout<<"PASS UART-owner TCP open/binary write/hex read/close, no Ctrl-Z, payload cannot forge URCs and real SIM notification still imports\n";
    }
    {
        Harness h(2000);void* request=nullptr;bool done=false;uint32_t openedAt=0;std::string store="SM";
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CPMS=\"ME\"")store="ME";
            if(cmd=="AT+CPMS=\"SM\"")store="SM";
            if(cmd=="AT+NETOPEN?"){platform::modemUart.feed("+NETOPEN: 0\r\nOK\r\n");return true;}
            if(cmd=="AT+NETOPEN"){openedAt=current+200;platform::modemUart.feed("OK\r\n");return true;}
            if(cmd.rfind("AT+CIPOPEN=9,",0)==0){platform::modemUart.feed("OK\r\n+CIPOPEN: 9,3\r\n");return true;}
            if(cmd=="AT+CMGL=4" && store=="ME" && openedAt) {
                platform::modemUart.feed("+CMGL: 1,0,,20\r\n00000B915155214365F700006210610000000005E8329BFD06\r\nOK\r\n");return true;
            }
            return false;
        };
        tickHook=[&]{
            if(current==500){request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test");assert(request);}
            if(openedAt && current==openedAt)platform::modemUart.feed("+CMTI: \"ME\",1\r\n+NETOPEN: 0\r\n");
            int result=0;std::string data;int64_t timestamp=0;
            if(request && cellTcpTestTake(request,result,data,timestamp)){assert(result==-1);request=nullptr;done=true;}
        };
        h.run();assert(done && h.smsReady() && store=="SM" && h.count["AT+NETOPEN"]==1 && h.totalPrefix("AT+CIPSEND=")==0);
        unsigned received=0,failures=0;
        for(const auto& e:h.events()) {
            if(e.kind==CellEventKind::SmsIn)++received;
            if(e.kind==CellEventKind::Trace) {
                const std::string text=e.text;
                assert(text.find("example.test")==std::string::npos && text.find("CIPOPEN=")==std::string::npos);
                if(text.rfind("LTE socket failed:",0)==0) {
                    ++failures;
                    assert(text=="LTE socket failed: TCP open; asynchronous modem error; modem result=3");
                }
            }
        }
        assert(failures==1);
        assert(received==1 && h.totalPrefix("AT+CMGS=")==0);
        std::cout<<"PASS delayed NETOPEN and failed TCP retain ME notification with one sanitized stage diagnostic and no application data\n";
    }
    {
        Harness h(2000);void* request=nullptr;unsigned phase=0;bool networkOpen=false;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CIPMODE=0"){assert(false && "Do not rewrite mode while packet service is open");}
            if(cmd=="AT+NETOPEN?"){platform::modemUart.feed(std::string("+NETOPEN: ")+(networkOpen?"1":"0")+"\r\nOK\r\n");return true;}
            if(cmd=="AT+NETOPEN"){networkOpen=true;platform::modemUart.feed("OK\r\n+NETOPEN: 0\r\n");return true;}
            if(cmd.rfind("AT+CIPOPEN=9,",0)==0){platform::modemUart.feed("OK\r\n+CIPOPEN: 9,0\r\n");return true;}
            if(cmd=="AT+CIPCLOSE=9"){platform::modemUart.feed("OK\r\n+CIPCLOSE: 9,0\r\n");return true;}
            return false;
        };
        tickHook=[&]{
            if(current==500){request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test");assert(request);}
            int result=0;std::string data;int64_t timestamp=0;
            if(request && cellTcpTestTake(request,result,data,timestamp)) {
                assert(result==1);request=nullptr;++phase;
                if(phase==1 || phase==3)request=cellTcpTestSubmit(CellTcpTestKind::Close);
                else if(phase==2)request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test");
                if(phase<4)assert(request);
            }
        };
        h.run();assert(phase==4 && networkOpen && h.count["AT+CIPMODE?"]==2 && h.count["AT+CIPMODE=0"]==0);
        assert(h.count["AT+NETOPEN"]==1 && h.totalPrefix("AT+CIPOPEN=")==2 && h.count["AT+CIPCLOSE=9"]==2);
        assert(h.totalPrefix("AT+CIPSEND=")==0 && h.totalPrefix("AT+NETCLOSE")==0 && h.count["AT+CFUN=1,1"]==0);
        for(const auto& e:h.events())if(e.kind==CellEventKind::Trace)assert(std::string(e.text).rfind("LTE socket failed:",0)!=0);
        std::cout<<"PASS two explicit TCP turns reuse NETOPEN without redundant mode writes, network close, reset or application sends\n";
    }
    {
        Harness h(1000);void* request=nullptr;bool done=false,modeSet=false;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+CIPMODE?"){platform::modemUart.feed("+CIPMODE: 1\r\nOK\r\n");return true;}
            if(cmd=="AT+CIPMODE=0")modeSet=true;
            if(cmd=="AT+CIPRXGET=1")assert(modeSet);
            if(cmd=="AT+NETOPEN?"){platform::modemUart.feed("+NETOPEN: 1\r\nOK\r\n");return true;}
            if(cmd.rfind("AT+CIPOPEN=9,",0)==0){platform::modemUart.feed("OK\r\n+CIPOPEN: 9,0\r\n");return true;}
            return false;
        };
        tickHook=[&]{
            if(current==500){request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test");assert(request);}
            int result=0;std::string data;int64_t timestamp=0;
            if(request && cellTcpTestTake(request,result,data,timestamp)){assert(result==1);request=nullptr;done=true;}
        };
        h.run();assert(done && modeSet && h.count["AT+CIPMODE?"]==1 && h.count["AT+CIPMODE=0"]==1);
        assert(h.totalPrefix("AT+CIPSEND=")==0 && h.totalPrefix("AT+NETCLOSE")==0);
        std::cout<<"PASS queried transparent mode selects nontransparent mode once before continuing socket setup\n";
    }
    {
        for(const std::string reply:{"", "+CIPMODE: 2\r\n", "+CIPMODE: 00\r\n", "+CIPMODE: \"0\"\r\n", "+CIPMODE: 0,1\r\n", "+CIPMODE: 0\r\n+CIPMODE: 1\r\n", "+CIPMODE: 0\r\nunexpected\r\n"}) {
            Harness h(1000);void* request=nullptr;bool done=false;
            h.custom=[&](const std::string& cmd,unsigned){
                if(cmd=="AT+CIPMODE?"){platform::modemUart.feed(reply+"OK\r\n");return true;}
                return false;
            };
            tickHook=[&]{
                if(current==500){request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test");assert(request);}
                int result=0;std::string data;int64_t timestamp=0;
                if(request && cellTcpTestTake(request,result,data,timestamp)){assert(result==-1);request=nullptr;done=true;}
            };
            h.run();assert(done && h.count["AT+CIPMODE?"]==1 && h.count["AT+CIPMODE=0"]==0 && h.count["AT+CIPRXGET=1"]==0);
            assert(h.totalPrefix("AT+NETOPEN")==0 && h.totalPrefix("AT+CIPOPEN=")==0 && h.totalPrefix("AT+CIPSEND=")==0);
            unsigned failures=0;for(const auto& e:h.events())if(e.kind==CellEventKind::Trace && std::string(e.text).rfind("LTE socket failed:",0)==0) {
                ++failures;assert(std::string(e.text)=="LTE socket failed: TCP mode query; invalid TCP mode; modem result=-1");
            }
            assert(failures==1);
        }
        std::cout<<"PASS missing, invalid, duplicate and unexpected TCP mode query replies fail closed without setup writes\n";
    }
    {
        Harness h(7500);void* request=nullptr;bool cancelled=false;
        h.custom=[&](const std::string& cmd,unsigned){
            if(cmd=="AT+NETOPEN?"){platform::modemUart.feed("+NETOPEN: 1\r\nOK\r\n");return true;}
            if(cmd.rfind("AT+CIPOPEN=9,",0)==0){platform::modemUart.feed("OK\r\n");return true;}
            return false;
        };
        tickHook=[&]{
            if(current==500){request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test");assert(request);}
            if(current==600)platform::modemUart.feed("RDY\r\n");
            if(current==700)platform::modemUart.feed("+CIPOPEN: 9,0\r\n"); // Stale result from before the reset.
            int result=0;std::string data;int64_t timestamp=0;
            if(request && cellTcpTestTake(request,result,data,timestamp)){assert(result==-1);request=nullptr;cancelled=true;}
        };
        h.run();assert(cancelled && h.smsReady() && cellTcpReady() && h.count["AT"]==2);
        assert(h.totalPrefix("AT+CIPOPEN=")==1 && h.totalPrefix("AT+CMGS=")==0 && h.count["AT+CFUN=1,1"]==0);
        std::cout<<"PASS modem restart cancels socket request, ignores stale result and restores SMS without socket/SMS replay\n";
    }
    {
        Harness h(3000);void* request=nullptr;bool expired=false;
        h.custom=[&](const std::string& cmd,unsigned){return !cmd.empty() && cmd.back()=='\x1a';};
        tickHook=[&]{
            if(current==500)h.action(CellActionKind::Sms,91);
            if(current==600){request=cellTcpTestSubmit(CellTcpTestKind::Open,"example.test",443,500);assert(request);}
            if(current==2000)platform::modemUart.feed("+CMGS: 91\r\nOK\r\n");
            int result=0;std::string data;int64_t timestamp=0;
            if(request && cellTcpTestTake(request,result,data,timestamp)){assert(result==-1);request=nullptr;expired=true;}
        };
        h.run();assert(expired && h.smsReady() && h.totalPrefix("AT+CMGS=")==1);
        assert(h.count["AT+CIPMODE?"]==0 && h.count["AT+CIPMODE=0"]==0 && h.totalPrefix("AT+CIPOPEN=")==0 && h.count["AT+CFUN=1,1"]==0);
        std::cout<<"PASS socket request expires while waiting behind an SMS and never starts a stale network operation or resends SMS\n";
    }
#endif
    vQueueDelete(cellEvents);vQueueDelete(cellActions);cellEvents=cellActions=nullptr;
}
