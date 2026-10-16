#pragma once
#include <atomic>
#include <cstdint>
namespace deck {
// Separates the user's radio preference from temporary RF suspension. Tickets
// invalidate queued work across OFF/ON and Airplane transitions.
class RadioGate {
public:
    void prefer(bool enabled){update(1,enabled,true);}
    void suspend(bool value){update(2,value,false);}
    bool preferred()const{return state_.load()&1;}
    bool suspended()const{return state_.load()&2;}
    bool allowed()const{return (state_.load()&3)==1;}
    uint32_t ticket()const{return state_.load();}
    bool accepts(uint32_t ticket)const{return (ticket&3)==1 && state_.load()==ticket;}
private:
    std::atomic<uint32_t> state_{0};
    void update(uint32_t bit,bool value,bool renew){
        auto old=state_.load();
        for(;;){
            if(!renew && bool(old&bit)==value)return;
            const auto next=((old+4)&~uint32_t(3))|((old&3&~bit)|(value?bit:0));
            if(state_.compare_exchange_weak(old,next))return;
        }
    }
};
}
