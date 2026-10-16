#include <radio_gate.h>
#include <cassert>
#include <iostream>
int main(){
    deck::RadioGate gate;
    assert(!gate.preferred() && !gate.allowed());
    gate.prefer(true);const auto scan=gate.ticket();assert(gate.accepts(scan));
    gate.suspend(true);assert(gate.preferred() && !gate.allowed() && !gate.accepts(scan));
    // A queued Enable changes the saved preference, never the suspension.
    gate.prefer(true);const auto whileOff=gate.ticket();assert(!gate.accepts(whileOff));
    gate.suspend(false);assert(gate.allowed() && !gate.accepts(scan) && !gate.accepts(whileOff));
    const auto fresh=gate.ticket();gate.suspend(false);assert(gate.accepts(fresh));
    gate.prefer(false);gate.prefer(true);assert(!gate.accepts(fresh));
    gate.suspend(true);gate.prefer(false);gate.suspend(false);assert(!gate.preferred() && !gate.allowed());
    for(unsigned n=0;n<10000;++n){
        gate.prefer(true);const auto queued=gate.ticket();
        gate.suspend(true);gate.suspend(false);
        assert(gate.allowed() && !gate.accepts(queued));
        gate.prefer(false);gate.suspend(true);gate.suspend(false);
        assert(!gate.allowed());
    }
    std::cout<<"PASS radio preference restoration and stale queued-work rejection across Airplane/OFF transitions\n";
}
