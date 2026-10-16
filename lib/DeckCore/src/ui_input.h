#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace deck {
// Navigation is ready immediately. Typing waits briefly for a burst to settle,
// with a hard deadline so continuous typing still gets regular display updates.
struct UiRefreshState {
    bool pending=true,urgent=true;
    uint32_t version=1,firstEdit=0,lastEdit=0;
    void changed(uint32_t now,bool typing=false) {
        if(!pending)firstEdit=now;
        pending=true;urgent=urgent || !typing;lastEdit=now;++version;
    }
    bool ready(uint32_t now)const {
        return pending && (urgent || uint32_t(now-lastEdit)>=100 || uint32_t(now-firstEdit)>=250);
    }
    void rendered(uint32_t snapshot) {
        if(snapshot==version){pending=false;urgent=false;}
    }
};
// Busy callbacks collect decoded keys and their capture time. Applying them
// after the panel completes must not restart a quiet interval already elapsed.
// On overflow discard the entire
// batch, including Enter, instead of submitting a silently truncated message.
template<size_t Capacity> class UiKeyBuffer {
    static_assert(Capacity>0,"A key buffer needs capacity");
    struct Event {char value=0;uint32_t capturedAt=0;};
    std::array<Event,Capacity> data_{};
    size_t start_=0,size_=0;
    bool overflow_=false;
    uint32_t dropped_=0;
public:
    bool push(char value,uint32_t capturedAt) {
        if(overflow_){++dropped_;return false;}
        if(size_==Capacity){dropped_+=uint32_t(size_+1);data_.fill({});start_=size_=0;overflow_=true;return false;}
        data_[(start_+size_)%Capacity]={value,capturedAt};++size_;return true;
    }
    bool pop(char& value,uint32_t& capturedAt) {
        if(!size_ || overflow_)return false;
        value=data_[start_].value;capturedAt=data_[start_].capturedAt;data_[start_]={};
        start_=(start_+1)%Capacity;--size_;return true;
    }
    void reset(){data_.fill({});start_=size_=0;overflow_=false;}
    bool overflowed()const{return overflow_;}
    size_t size()const{return size_;}
    uint32_t dropped()const{return dropped_;}
};
}
