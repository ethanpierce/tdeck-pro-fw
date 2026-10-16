#include "modem_task.h"
#include "board.h"
#include "settings.h"
#include <at_engine.h>
#include <sms_pdu.h>
#include <apn.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

QueueHandle_t cellEvents=nullptr, cellActions=nullptr;
std::atomic<uint32_t> droppedCellEvents{0};
namespace {
constexpr uint32_t ReconnectMs=5000, PollMs=15000, CallPollMs=3000;
const char* const DiagnosticCommands[]={"AT+CPIN?","AT+CEREG?","AT+CREG?","AT+CSQ","AT+COPS?",
    "AT+CGMM","AT+CGMR","AT+CFUN?","AT+CPSI?"};

bool emit(CellEventKind kind, const std::string& text="", int value=0, uint32_t id=0, const std::string& peer="") {
    CellEvent e{}; e.kind=kind; e.value=value; e.id=id;
    snprintf(e.peer,sizeof(e.peer),"%s",peer.c_str());
    snprintf(e.text,sizeof(e.text),"%s",text.c_str());
    // A full UI queue must not block UART draining for every diagnostic line.
    TickType_t wait=kind==CellEventKind::Trace?0:pdMS_TO_TICKS(100);
    if (xQueueSend(cellEvents,&e,wait)==pdTRUE) return true;
    droppedCellEvents.fetch_add(1,std::memory_order_relaxed);
    return false;
}
std::string valueAfterColon(const std::string& line) {
    auto start=line.find(':');
    if (start==std::string::npos) return line;
    start=line.find_first_not_of(" \t",start+1);
    return start==std::string::npos?"":line.substr(start);
}
int number(const std::string& field, int low, int high) {
    if (field.empty()) return -1;
    char* end=nullptr;
    long value=strtol(field.c_str(),&end,10);
    return end && !*end && value>=low && value<=high?int(value):-1;
}
std::string callerNumber(const std::vector<std::string>& fields, size_t index) {
    if (fields.size()<=index) return "";
    std::string peer=fields[index];
    if (!peer.empty() && peer[0]!='+' && fields.size()>index+1 && fields[index+1]=="145") peer="+"+peer;
    return peer;
}
class CellService {
public:
    CellService(): engine_([](const std::string& data){Serial1.write(reinterpret_cast<const uint8_t*>(data.data()),data.size());},
        [this](const std::string& line){urc(line);}) {}
    void run() {
        Serial1.setRxBufferSize(8192);
        Serial1.begin(DECK_MODEM_BAUD,SERIAL_8N1,board::ModemEspRx,board::ModemEspTx);
        board::startModemPower();
        begin();
        for (;;) {
            engine_.tick(millis());
            while (Serial1.available()) engine_.feed(char(Serial1.read()));
            CellAction action{};
            if (xQueueReceive(cellActions,&action,0)==pdTRUE) handle(action);
            uint32_t now=millis();
            if (engine_.idle()) {
                // Only enqueue a call when the UART is free. A queued hangup can
                // then replace an unsent dial instead of running before it.
                if (hasPendingCall_) performCall();
                else if (!linked_) {
                    if (uint32_t(now-lastConnect_)>=ReconnectMs) begin();
                } else if (!configuring_) {
                    if(apnPending_) applyApn();
                    else if (sim_ && !smsReady() && !smsConfiguring_ &&
                        (!smsSetupAttempted_ || uint32_t(now-lastSmsSetup_)>=PollMs)) {
                        smsConfiguring_=true; smsSetupAttempted_=true; lastSmsSetup_=now;
                        configureSms(0,simGeneration_);
                    } else if (callPollSoon_ || uint32_t(now-lastCallPoll_)>=(inCall_?CallPollMs:PollMs)) {
                        callPollSoon_=false; lastCallPoll_=now; calls();
                    } else if (diagnosticStep_<diagnosticCount_) {
                        query(DiagnosticCommands[diagnosticStep_++]);
                    } else if (smsReady() && scanRequired_ && !scanning_ &&
                        (!scanAttempted_ || uint32_t(now-lastScan_)>=PollMs)) inbox();
                    else if (uint32_t(now-lastPoll_)>=PollMs) diagnostics(false);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
private:
    bool smsReady() const { return linked_ && sim_ && pdu_ && storage_ && notifications_; }
    bool registered() const { return reg_==1 || reg_==5 || reg_==9 || reg_==10; }
    bool smsRegistered() const { return registered() || reg_==6 || reg_==7; }
    void offline(const std::string& reason) {
        ++generation_; ++simGeneration_;
        linked_=sim_=pdu_=storage_=notifications_=false;
        configuring_=smsConfiguring_=false;
        smsSetupAttempted_=scanAttempted_=false;
        scanRequired_=true; simStatus_.clear(); reg_=-1;
        diagnosticStep_=diagnosticCount_=0;
        lastConnect_=millis();
        if (hasPendingCall_) {
            hasPendingCall_=false;
            emit(CellEventKind::Call,"Call command cancelled while modem reconnects",-1);
        }
        if (inCall_) emit(CellEventKind::Call,"Call state unknown while modem reconnects",-1);
        inCall_=false;
        emit(CellEventKind::Link,reason,0);
        emit(CellEventKind::Sim,"Checking",0);
        emit(CellEventKind::SmsReady,"",0);
    }
    void begin() {
        offline("Checking modem...");
        const uint32_t generation=generation_;
        send("AT",[this,generation](const deck::AtResult& r){
            if (generation!=generation_) return;
            linked_=r.ok;
            emit(CellEventKind::Link,r.ok?"Modem responding":"No AT response; retrying without resetting modem",r.ok);
            if (r.ok) {configuring_=true; configure(0,generation);}
        },2000);
    }
    bool send(std::string cmd, std::function<void(const deck::AtResult&)> done={}, uint32_t timeout=10000,
              std::string payload="", bool call=false) {
        const uint32_t generation=generation_;
        auto callback=[this,generation,done=std::move(done)](const deck::AtResult& r){
            if (r.uncertain && generation==generation_) offline("Modem response uncertain; reconnecting. No automatic resend.");
            if (done) done(r);
        };
        deck::AtCommand c{std::move(cmd),timeout,std::move(payload),call,callback};
        if (engine_.enqueue(std::move(c),call)) return true;
        callback({false,false,"Modem queue full",{}});
        return false;
    }
    void configure(unsigned step, uint32_t generation) {
        static const char* cmds[]={"ATE0","AT+CMEE=2","AT+CEREG=2","AT+CREG=2","AT+CLIP=1"};
        if (step>=sizeof(cmds)/sizeof(cmds[0])) {
            apnPending_=!apn_.empty();
            configuring_=false; callPollSoon_=true; diagnostics(true); return;
        }
        send(cmds[step],[this,step,generation](const deck::AtResult& r){
            if (generation!=generation_ || !linked_ || r.final.rfind("Cancelled",0)==0) return;
            if (!r.ok) emit(CellEventKind::Trace,std::string("Setup: ")+r.final);
            configure(step+1,generation);
        });
    }
    void configureSms(unsigned step, uint32_t simGeneration) {
        static const char* cmds[]={"AT+CMGF=0","AT+CPMS=\"SM\",\"SM\",\"SM\"","AT+CNMI=2,1,0,0,0"};
        if (step>=sizeof(cmds)/sizeof(cmds[0])) {
            smsConfiguring_=false;
            emit(CellEventKind::SmsReady,"",smsReady());
            return;
        }
        send(cmds[step],[this,step,simGeneration](const deck::AtResult& r){
            if (!linked_ || !sim_ || simGeneration!=simGeneration_) return;
            if (step==0) pdu_=r.ok;
            if (step==1) storage_=r.ok;
            if (step==2) notifications_=r.ok;
            if (!r.ok) {
                smsConfiguring_=false;
                emit(CellEventKind::Trace,"SMS setup: "+r.final+"; retrying after SIM readiness");
                emit(CellEventKind::SmsReady,"",0);
                return;
            }
            configureSms(step+1,simGeneration);
        });
    }
    void diagnostics(bool full) {
        if (!linked_) return;
        diagnosticStep_=0; diagnosticCount_=full?9:5; lastPoll_=millis();
    }
    void simStatus(const std::string& status) {
        bool ready=status=="READY";
        if (ready!=sim_) {
            ++simGeneration_;
            sim_=ready;
            pdu_=storage_=notifications_=false;
            smsConfiguring_=smsSetupAttempted_=false;
            scanRequired_=true; scanAttempted_=false;
        }
        if (status!=simStatus_) {
            simStatus_=status;
            emit(CellEventKind::Sim,status,sim_);
            emit(CellEventKind::SmsReady,"",smsReady());
        }
    }
    void query(const std::string& cmd) {
        const uint32_t generation=generation_;
        send(cmd,[this,cmd,generation](const deck::AtResult& r){
            if (generation!=generation_ || !linked_) return;
            if (!r.ok) {
                emit(CellEventKind::Trace,cmd+": "+r.final);
                if (cmd=="AT+CPIN?") simStatus(r.final);
                return;
            }
            for (const auto& line:r.lines) {
                emit(CellEventKind::Trace,line);
                if (line.rfind("+CPIN:",0)==0) simStatus(valueAfterColon(line));
                else if (line.rfind("+CEREG:",0)==0) network(line);
                else if (line.rfind("+CSQ:",0)==0) {
                    auto f=deck::csv(valueAfterColon(line));
                    int value=f.empty()?-1:number(f[0],0,31);
                    emit(CellEventKind::Signal,"",value>=0?-113+2*value:0);
                } else if (line.rfind("+COPS:",0)==0) emit(CellEventKind::Operator,valueAfterColon(line));
                else if (line.rfind("+CPSI:",0)==0) emit(CellEventKind::System,valueAfterColon(line));
            }
        });
    }
    void network(const std::string& line) {
        int r=deck::registration(line);
        if (r>=0) { reg_=r; emit(CellEventKind::Registration,"",r); }
    }
    void urc(const std::string& line) {
        if (line=="RDY") offline("Modem restarted; reconnecting without retrying messages or calls");
        else if (line.rfind("+CPIN:",0)==0) simStatus(valueAfterColon(line));
        else if (line=="SMS DONE" || line=="SMS Ready") smsSetupAttempted_=false;
        else if (line.rfind("+CEREG:",0)==0) network(line);
        else if (line.rfind("+CMTI:",0)==0) {
            auto f=deck::csv(valueAfterColon(line));
            if (f.size()==2 && f[0]=="SM") {
                int slot=number(f[1],1,999);
                if (slot>0 && smsReady() && engine_.idle()) readSlot(slot);
                else scanRequired_=true;
            } else emit(CellEventKind::Trace,"SMS in unsupported storage; only SIM (SM) inbox is read");
        } else if (line=="RING" || line.rfind("+CRING:",0)==0) {
            inCall_=true; callPollSoon_=true; emit(CellEventKind::Call,"Incoming call",4);
        } else if (line.rfind("+CLIP:",0)==0) {
            auto f=deck::csv(valueAfterColon(line));
            if (!f.empty()) {inCall_=true; callPollSoon_=true; emit(CellEventKind::Call,"Incoming call",4,0,callerNumber(f,0));}
        } else if (line=="NO CARRIER" || line=="BUSY" || line=="NO ANSWER" ||
                   line=="NO DIALTONE" || line=="NO DIAL TONE") {
            inCall_=false; emit(CellEventKind::Call,line,-1);
        } else if (line.rfind("VOICE CALL",0)==0) callPollSoon_=true;
    }
    bool parsePdus(const deck::AtResult& r) {
        bool expected=false, received=false, delivered=true;
        for (const auto& line:r.lines) {
            if (line.rfind("+CMGR:",0)==0 || line.rfind("+CMGL:",0)==0) {
                auto fields=deck::csv(valueAfterColon(line));
                size_t statusIndex=line.rfind("+CMGL:",0)==0?1:0;
                received=fields.size()>statusIndex && number(fields[statusIndex],0,1)>=0;
                expected=true; continue;
            }
            if (!expected) continue;
            expected=false;
            if (!received) continue; // Stored SMS-SUBMIT entries are not an inbox message.
            deck::SmsDeliver message;
            if (deck::decodeSms(line,message)) {
                uint32_t id=deck::fingerprint(line);
                if (std::find(seen_.begin(),seen_.end(),id)==seen_.end()) {
                    if (emit(CellEventKind::SmsIn,message.text,0,id,message.sender)) {
                        if (seen_.size()==256) seen_.pop_front();
                        seen_.push_back(id);
                    } else delivered=false; // Message remains on SIM; a later scan retries UI delivery.
                }
            } else emit(CellEventKind::Trace,"Unsupported SMS kept on SIM (multipart/sender/encoding)");
        }
        if (!r.ok) emit(CellEventKind::Trace,"Inbox: "+r.final);
        return delivered && !expected && r.ok;
    }
    void readSlot(int index) {
        send("AT+CMGR="+std::to_string(index),[this](const deck::AtResult& r){
            if (!parsePdus(r)) scanRequired_=true;
        });
    }
    void inbox() {
        if (scanning_) return;
        // Stream each header/PDU pair; SIM capacity must not determine heap use.
        auto header=std::make_shared<std::string>();
        scanning_=true; scanRequired_=false; scanAttempted_=true; lastScan_=millis();
        const uint32_t generation=generation_;
        deck::AtCommand command{"AT+CMGL=4",30000,"",false,
            [this,header,generation](const deck::AtResult& r){
                scanning_=false;
                if (r.uncertain && generation==generation_) offline("Inbox timeout; reconnecting modem");
                if (!r.ok || !header->empty()) {
                    scanRequired_=true;
                    emit(CellEventKind::Trace,"Inbox scan: "+(r.ok?std::string("incomplete PDU"):r.final));
                }
            },
            [this,header](const std::string& line){
                if (line.rfind("+CMGL:",0)==0) {*header=line;return;}
                if (!header->empty()) {
                    if (!parsePdus({true,false,"OK",{*header,line}})) scanRequired_=true;
                    header->clear();
                }
            }};
        if (!engine_.enqueue(std::move(command))) {
            scanning_=false; scanRequired_=true;
            emit(CellEventKind::Trace,"Inbox scan: modem queue full");
        }
    }
    void calls() {
        const uint32_t generation=generation_;
        send("AT+CLCC",[this,generation](const deck::AtResult& r){
            if (!r.ok || generation!=generation_) return;
            // Count the interval from completion so a slow CLCC response does
            // not cause another CLCC immediately and starve inbox/diagnostics.
            lastCallPoll_=millis();
            int selected=-1, rank=-1;
            std::string peer;
            static const char* names[]={"Active","Held","Dialing","Ringing remotely","Incoming call","Waiting call"};
            static const int ranks[]={4,1,2,3,6,5};
            for (const auto& line:r.lines) if (line.rfind("+CLCC:",0)==0) {
                auto f=deck::csv(valueAfterColon(line));
                if (f.size()<5) continue;
                int state=number(f[2],0,6), mode=number(f[3],0,9);
                if (state<0 || state>5 || (mode!=0 && mode!=9)) continue;
                if (ranks[state]>rank) {selected=state; rank=ranks[state]; peer=callerNumber(f,5);}
            }
            inCall_=selected>=0;
            if (inCall_) emit(CellEventKind::Call,names[selected],selected,0,peer);
            else emit(CellEventKind::Call,"Idle",-1);
        });
    }
    void performCall() {
        CellAction a=pendingCall_; hasPendingCall_=false;
        if (a.kind==CellActionKind::Dial && (!linked_ || !sim_ || !registered() || !deck::phoneNumber(a.peer))) {
            emit(CellEventKind::Call,"Cannot dial: check number, SIM and LTE registration",-1); return;
        }
        if (a.kind==CellActionKind::Answer && (!linked_ || !sim_)) {
            emit(CellEventKind::Call,"Cannot answer: modem or SIM unavailable",-1); return;
        }
        // ATH may be ignored with OK when CVHU=1. CHUP explicitly cancels voice calls.
        std::string command=a.kind==CellActionKind::Dial?"ATD"+std::string(a.peer)+";":
            a.kind==CellActionKind::Answer?"ATA":"AT+CHUP";
        send(command,[this,kind=a.kind](const deck::AtResult& r){
            if (!r.ok) {emit(CellEventKind::Call,r.final,-1); return;}
            callPollSoon_=true;
            if (kind==CellActionKind::Hangup) {
                inCall_=false; emit(CellEventKind::Call,"Hangup accepted; checking call",-1);
            } else {
                inCall_=true; emit(CellEventKind::Call,"Command accepted; checking call",2);
            }
        },15000,"",true);
    }
    void applyApn() {
        apnPending_=false;
        const std::string requested=apn_;
        send("AT+CGDCONT?",[this,requested](const deck::AtResult& r){
            if(requested!=apn_) return; // A newer edit superseded this request.
            if(r.uncertain) {emit(CellEventKind::ApnResult,"APN saved; modem response uncertain",0);return;}
            for(const auto& line:r.lines) if(line.rfind("+CGDCONT:",0)==0) {
                auto fields=deck::csv(valueAfterColon(line));
                if(fields.size()>=3 && fields[0]=="1" && fields[1]=="IPV4V6" && fields[2]==requested) {
                    emit(CellEventKind::ApnResult,"APN already applied: "+requested,1);return;
                }
            }
            send("AT+CGDCONT=1,\"IPV4V6\",\""+requested+"\"",[requested](const deck::AtResult& result){
                emit(CellEventKind::ApnResult,result.ok?"APN applied: "+requested:
                    "APN saved; modem apply failed: "+result.final,result.ok);
            });
        });
    }
    void networkQuery(const std::string& cmd) {
        static const char* allowed[]={"AT+CGDCONT?","AT+CGATT?","AT+CGACT?","AT+CNMP?",
            "AT+CNBP?","AT+CEER","AT+CEREG?","AT+CGREG?","AT+CEREG=?",
            "AT+CFUN?","AT+CSQ","AT+COPS?","AT+CPSI?","AT+COPS=?"};
        bool accepted=false;
        for(const char* value:allowed) if(cmd==value) accepted=true;
        if(!accepted) {emit(CellEventKind::Trace,"Unsupported read-only modem query");return;}
        if(!linked_ || configuring_ || !engine_.idle() || inCall_ || hasPendingCall_ || smsBusy_) {
            emit(CellEventKind::Trace,"Modem busy or offline; retry the query shortly");return;
        }
        emit(CellEventKind::Trace,"MODEM > "+cmd);
        send(cmd,[cmd](const deck::AtResult& r){
            for(const auto& line:r.lines)
                for(size_t offset=0;offset<line.size();offset+=480)
                    emit(CellEventKind::Trace,line.substr(offset,480));
            emit(CellEventKind::Trace,cmd+": "+r.final);
        },cmd=="AT+COPS=?"?180000:10000);
    }
    void handle(const CellAction& a) {
        if(a.kind==CellActionKind::SetApn) {
            if(!deck::validApn(a.text)) {emit(CellEventKind::ApnResult,"Invalid APN",0);return;}
            apn_=a.text; apnPending_=true;
            return;
        }
        if(a.kind==CellActionKind::NetworkQuery) {networkQuery(a.text);return;}
        if (a.kind==CellActionKind::Refresh) {
            if (engine_.idle()) begin();
            else emit(CellEventKind::Trace,"Modem busy; refresh again after the current operation");
            return;
        }
        if (a.kind==CellActionKind::ReadInbox) {
            if (smsReady()) {scanRequired_=true; scanAttempted_=false;}
            else emit(CellEventKind::Trace,"SIM inbox unavailable until SMS setup completes");
            return;
        }
        if (a.kind==CellActionKind::Sms) {
            deck::SmsSubmit pdu;
            if (!smsReady() || !smsRegistered()) {
                emit(CellEventKind::Failed,"SMS unavailable: check SIM, network and PDU setup",0,a.id); return;
            }
            if (smsBusy_) {emit(CellEventKind::Failed,"An SMS is already in progress; wait for its result",0,a.id); return;}
            if (!deck::encodeSms(a.peer,a.text,pdu)) {emit(CellEventKind::Failed,"Use a valid number and one GSM-7 SMS",0,a.id); return;}
            smsBusy_=true;
            send("AT+CMGS="+std::to_string(pdu.tpduBytes),[this,id=a.id](const deck::AtResult& r){
                smsBusy_=false;
                bool submitted=false;
                for (const auto& l:r.lines) if (l.rfind("+CMGS:",0)==0) submitted=true;
                if (r.ok && submitted) emit(CellEventKind::SmsSent,"Accepted by modem; no delivery receipt",0,id);
                else emit(CellEventKind::Failed,r.ok?"No SMS reference; result unknown":r.final,r.uncertain || r.ok,id);
            },60000,pdu.hex);
            return;
        }
        // Keep only the newest unsent call request. In particular, hangup must
        // cancel an unsent dial rather than dial after a successful hangup.
        pendingCall_=a; hasPendingCall_=true;
    }
    deck::AtEngine engine_;
    std::deque<uint32_t> seen_;
    CellAction pendingCall_{};
    std::string simStatus_;
    std::string apn_;
    uint32_t generation_=0, simGeneration_=0, lastConnect_=0, lastPoll_=0, lastCallPoll_=0, lastSmsSetup_=0, lastScan_=0;
    unsigned diagnosticStep_=0, diagnosticCount_=0;
    bool linked_=false, sim_=false, pdu_=false, storage_=false, notifications_=false, inCall_=false;
    bool configuring_=false, smsConfiguring_=false, smsSetupAttempted_=false, smsBusy_=false;
    bool scanRequired_=true, scanning_=false, scanAttempted_=false, callPollSoon_=false, hasPendingCall_=false;
    bool apnPending_=false;
    int reg_=-1;
};
void worker(void*) { CellService service; service.run(); }
}
bool startCellTask() {
    cellEvents=xQueueCreate(24,sizeof(CellEvent));
    cellActions=xQueueCreate(6,sizeof(CellAction));
    if (cellEvents && cellActions && xTaskCreatePinnedToCore(worker,"cellular",12288,nullptr,2,nullptr,0)==pdPASS) return true;
    if (cellEvents) vQueueDelete(cellEvents);
    if (cellActions) vQueueDelete(cellActions);
    cellEvents=cellActions=nullptr;
    return false;
}
