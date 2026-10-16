// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace deck {
enum class PowerButtonEvent {None,Shutdown,Force};
class PowerButton {
public:
    static constexpr uint32_t DebounceMs=40,ShutdownMs=5000,ForceMs=30000;
    PowerButtonEvent sample(bool down,uint32_t now) {
        if(!seen_){seen_=true;raw_=stable_=down;rawAt_=now;return PowerButtonEvent::None;}
        if(raw_!=down){raw_=down;rawAt_=now;}
        if(!armed_) {
            // BOOT held during reset/flashing must not become an OS power hold.
            if(!raw_ && uint32_t(now-rawAt_)>=DebounceMs){armed_=true;stable_=false;}
            return PowerButtonEvent::None;
        }
        if(raw_!=stable_ && uint32_t(now-rawAt_)>=DebounceMs) {
            stable_=raw_;tracking_=stable_;
            if(stable_){pressAt_=rawAt_;shutdownSent_=forceSent_=false;}
        }
        if(tracking_ && raw_) {
            const auto held=uint32_t(now-pressAt_);
            if(held>=ForceMs && !forceSent_){forceSent_=shutdownSent_=true;return PowerButtonEvent::Force;}
            if(held>=ShutdownMs && !shutdownSent_){shutdownSent_=true;return PowerButtonEvent::Shutdown;}
        }
        return PowerButtonEvent::None;
    }
    bool released()const{return armed_ && !raw_ && !stable_;}
private:
    uint32_t rawAt_=0,pressAt_=0;
    bool seen_=false,raw_=false,stable_=false,armed_=false,tracking_=false,shutdownSent_=false,forceSent_=false;
};
}
