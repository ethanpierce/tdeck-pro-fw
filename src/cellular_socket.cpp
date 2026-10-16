#include "cellular_socket.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <cstdlib>
#include <cellular_http_protocol.h>

enum Kind {Open,Write,Read,Close,Clock};
struct CellTcpRequest {
    Kind kind;std::string data;uint16_t port=0;uint32_t timeout=10000,enqueued=millis();
    int result=-1;int64_t timestamp=0;
    QueueHandle_t done=nullptr;std::atomic<unsigned> owners{1};
    explicit CellTcpRequest(Kind value):kind(value){done=xQueueCreate(1,sizeof(uint8_t));}
    void release(){if(owners.fetch_sub(1)==1){if(done)vQueueDelete(done);delete this;}}
};
namespace {
constexpr int Socket=9;
constexpr size_t WriteLimit=768,ReadLimit=256;
QueueHandle_t requests=nullptr;
std::atomic<bool> readyFlag{false};
bool exchange(CellTcpRequest* r) {
    if(!requests || !r->done){r->release();return false;}
    r->owners.fetch_add(1);
    if(xQueueSend(requests,&r,0)!=pdTRUE){r->release();r->release();return false;}
    uint8_t token=0;
    // The broker retains ownership after a caller timeout, so no stack pointer
    // or response buffer can be used after its caller has returned.
    if(xQueueReceive(r->done,&token,pdMS_TO_TICKS(r->timeout+2000))!=pdTRUE){r->release();return false;}
    return true;
}
int integer(const std::string& s,int max=65535) {
    if(s.empty() || s.size()>10){return -1;}unsigned long n=0;
    for(char c:s){if(c<'0'||c>'9'){return -1;}n=n*10+unsigned(c-'0');if(n>unsigned(max)){return -1;}}return int(n);
}
std::vector<std::string> fields(const std::string& line) {
    auto colon=line.find(':');return colon==std::string::npos?std::vector<std::string>{}:deck::csv(line.substr(colon+1));
}
bool hostValid(const std::string& host) {
    if(host.empty() || host.size()>191){return false;}
    for(char c:host)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')){return false;}
    return true;
}
}
bool cellTcpReady(){return readyFlag.load();}
bool cellTcpOpen(const std::string& host,uint16_t port,uint32_t timeout) {
    if(!hostValid(host)||!port){return false;}
    auto* r=new(std::nothrow) CellTcpRequest(Open);if(!r){return false;}
    r->data=host;r->port=port;r->timeout=std::min(timeout,60000u);
    if(!exchange(r)){return false;}bool ok=r->result==1;r->release();return ok;
}
int cellTcpWrite(const unsigned char* data,size_t size,uint32_t timeout) {
    if(!data||!size){return -1;}size=std::min(size,WriteLimit);
    auto* r=new(std::nothrow) CellTcpRequest(Write);if(!r){return -1;}
    r->data.assign(reinterpret_cast<const char*>(data),size);r->timeout=std::min(timeout,15000u);
    if(!exchange(r)){return -1;}int result=r->result;r->release();return result;
}
int cellTcpRead(unsigned char* data,size_t size,uint32_t timeout) {
    if(!data||!size){return -1;}
    auto* r=new(std::nothrow) CellTcpRequest(Read);if(!r){return -1;}
    r->port=uint16_t(std::min(size,ReadLimit));r->timeout=std::min(timeout,15000u);
    if(!exchange(r)){return -1;}int result=r->result;
    if(result>0 && size_t(result)<=size && r->data.size()==size_t(result))std::memcpy(data,r->data.data(),size_t(result));
    else if(result>0)result=-1;
    r->release();return result;
}
void cellTcpClose() {
    auto* r=new(std::nothrow) CellTcpRequest(Close);if(!r){return;}r->timeout=10000;
    if(exchange(r))r->release();
}
int64_t cellNetworkTime() {
    auto* r=new(std::nothrow) CellTcpRequest(Clock);if(!r){return 0;}
    if(!exchange(r)){return 0;}int64_t result=r->timestamp;r->release();return result;
}
bool CellularSocketBroker::begin() {
    if(!requests)requests=xQueueCreate(1,sizeof(CellTcpRequest*));
    return requests!=nullptr;
}
#ifdef DECK_CELLULAR_SOCKET_TEST
void* cellTcpTestSubmit(CellTcpTestKind kind,const std::string& data,uint16_t port,uint32_t timeout) {
    auto* r=new CellTcpRequest(static_cast<Kind>(kind));r->data=data;r->port=port;r->timeout=timeout;
    r->owners.fetch_add(1);
    if(!requests || xQueueSend(requests,&r,0)!=pdTRUE){r->release();r->release();return nullptr;}
    return r;
}
bool cellTcpTestTake(void* request,int& result,std::string& data,int64_t& timestamp) {
    auto* r=static_cast<CellTcpRequest*>(request);uint8_t token=0;
    if(!r || xQueueReceive(r->done,&token,0)!=pdTRUE){return false;}
    result=r->result;data=r->data;timestamp=r->timestamp;r->release();return true;
}
#endif
void CellularSocketBroker::finish(int result,const std::string& data,int64_t timestamp) {
    if(!active_){return;}
    auto* r=active_;active_=nullptr;++generation_;
    r->result=result;r->data=data;r->timestamp=timestamp;
    uint8_t token=1;xQueueSend(r->done,&token,0);r->release();
}
void CellularSocketBroker::cancel() {
    opened_=false;remoteClosed_=false;poisoned_=false;readyFlag=false;
    finish(-1);
    CellTcpRequest* r=nullptr;
    while(requests && xQueueReceive(requests,&r,0)==pdTRUE){r->result=-1;uint8_t token=1;xQueueSend(r->done,&token,0);r->release();}
}
void CellularSocketBroker::poll(deck::AtEngine& engine,bool ready,uint32_t now) {
    engine_=&engine;readyFlag=ready && !poisoned_;
    if(active_) {
        if(uint32_t(now-started_)>=active_->timeout){poisoned_=true;opened_=false;readyFlag=false;finish(-1);}
        return;
    }
    if(!engine.idle() || !requests){return;}
    CellTcpRequest* r=nullptr;if(xQueueReceive(requests,&r,0)!=pdTRUE){return;}
    active_=r;started_=r->enqueued;ack_=received_=false;asyncResult_=-1;++generation_;
    if(uint32_t(now-started_)>=r->timeout){finish(-1);return;}
    if(poisoned_ || (!ready && r->kind!=Close)){finish(-1);return;}
    if(r->kind==Open) {
        opened_=false;remoteClosed_=false;
        command("AT+CIPMODE=0",1);
    } else if(r->kind==Clock)command("AT+CCLK?",9);
    else if(r->kind==Close) {
        if(!opened_){finish(1);return;}
        command("AT+CIPCLOSE="+std::to_string(Socket),8);
    } else if(!opened_)finish(-1);
    else if(r->kind==Write) {
        if(remoteClosed_){finish(-1);return;}
        command("AT+CIPSEND="+std::to_string(Socket)+","+std::to_string(r->data.size()),6,r->data);
    } else command("AT+CIPRXGET=3,"+std::to_string(Socket)+","+std::to_string(r->port),7);
}
void CellularSocketBroker::command(const std::string& text,int next,const std::string& payload) {
    stage_=next;ack_=received_=false;asyncResult_=-1;
    const uint32_t generation=generation_;
    deck::AtCommand command{text,10000,payload,false,[this,next,generation](const deck::AtResult& r){completed(r,next,generation);}};
    command.appendPayloadCtrlZ=false;
    if(!engine_->enqueue(std::move(command))){if(next==8)opened_=false;finish(-1);}
}
void CellularSocketBroker::completed(const deck::AtResult& r,int next,uint32_t generation) {
    if(!active_ || generation!=generation_){return;}
    if(!r.ok) {
        if(next==7) {
            auto noDataText=[](const std::string& line){auto colon=line.find(':');return line.rfind("+IP ERROR:",0)==0 && colon!=std::string::npos && line.substr(colon+1).find_first_not_of(' ')!=std::string::npos && line.substr(colon+1).substr(line.substr(colon+1).find_first_not_of(' '))=="No data";};
            bool noData=noDataText(r.final);for(const auto& line:r.lines)if(noDataText(line))noData=true;
            if(noData){finish(remoteClosed_?-2:0);return;}
        }
        if(next==8 && remoteClosed_){opened_=false;finish(1);return;}
        if(next==8)opened_=false;
        if(r.uncertain){poisoned_=true;opened_=false;readyFlag=false;}
        finish(-1);return;
    }
    if(next==1)command("AT+CIPRXGET=1",2);
    else if(next==2)command("AT+NETOPEN?",3);
    else if(next==3) {
        int state=-1;for(const auto& line:r.lines)if(line.rfind("+NETOPEN:",0)==0){auto f=fields(line);if(f.size()==1)state=integer(f[0],1);}
        if(state==1)command("AT+CIPOPEN="+std::to_string(Socket)+",\"TCP\",\""+active_->data+"\","+std::to_string(active_->port),5);
        else if(state==0)command("AT+NETOPEN",4);
        else finish(-1);
    } else if(next==4 || next==5 || next==6 || next==8){ack_=true;advance();}
    else if(next==9) {
        int64_t timestamp=0;for(const auto& line:r.lines)if(line.rfind("+CCLK:",0)==0)timestamp=deck::cellClockUtc(line);
        finish(timestamp?1:-1,"",timestamp);
    } else if(next==7) {
        int count=-1;std::string data;bool header=false,bad=false;
        for(const auto& line:r.lines) {
            if(line.rfind("+CIPRXGET:",0)==0) {
                auto f=fields(line);
                if(header || f.size()!=4 || f[0]!="3" || integer(f[1],9)!=Socket){bad=true;break;}
                count=integer(f[2],active_->port);if(count<0 || integer(f[3],1048576)<0){bad=true;break;}header=true;
            } else if(header && count>0 && data.empty()) {
                if(!deck::cellHex(line,size_t(count),data)){bad=true;break;}
            } else {bad=true;break;}
        }
        if(bad || !header || count<0 || data.size()!=size_t(count)){poisoned_=true;opened_=false;readyFlag=false;finish(-1);}
        else finish(count?count:(remoteClosed_?-2:0),data);
    }
}
void CellularSocketBroker::advance() {
    if(!active_ || !ack_ || !received_){return;}
    if(asyncResult_!=0){if(stage_==6){poisoned_=true;opened_=false;readyFlag=false;}finish(-1);return;}
    if(stage_==4)command("AT+CIPOPEN="+std::to_string(Socket)+",\"TCP\",\""+active_->data+"\","+std::to_string(active_->port),5);
    else if(stage_==5){opened_=true;remoteClosed_=false;finish(1);}
    else if(stage_==6)finish(int(active_->data.size()));
    else if(stage_==8){opened_=false;finish(1);}
}
void CellularSocketBroker::onUrc(const std::string& line) {
    auto f=fields(line);
    if(line.rfind("+IPCLOSE:",0)==0 && f.size()>=1 && integer(f[0],9)==Socket){remoteClosed_=true;return;}
    if(line.rfind("+CIPEVENT:",0)==0){opened_=false;finish(-1);return;}
    if(!active_){return;}
    if(stage_==4 && line.rfind("+NETOPEN:",0)==0 && f.size()==1)asyncResult_=integer(f[0]);
    else if(stage_==5 && line.rfind("+CIPOPEN:",0)==0 && f.size()==2 && integer(f[0],9)==Socket)asyncResult_=integer(f[1]);
    else if(stage_==8 && line.rfind("+CIPCLOSE:",0)==0 && f.size()==2 && integer(f[0],9)==Socket)asyncResult_=integer(f[1]);
    else if(stage_==6 && line.rfind("+CIPSEND:",0)==0 && f.size()==3 && integer(f[0],9)==Socket) {
        const int requested=integer(f[1],int(WriteLimit)),confirmed=integer(f[2],int(WriteLimit));
        asyncResult_=requested==int(active_->data.size()) && confirmed==requested?0:-1;
    } else return;
    received_=true;advance();
}
