#include <power_button.h>
#include <cassert>
#include <iostream>
using deck::PowerButton;using Event=deck::PowerButtonEvent;
int main() {
    PowerButton p;
    assert(p.sample(true,0)==Event::None);
    assert(p.sample(true,40000)==Event::None); // Held at boot must first be released.
    p.sample(false,40001);p.sample(false,40041);assert(p.released());
    p.sample(true,40100);p.sample(false,40120);p.sample(false,40160);assert(p.released());
    p.sample(true,40200);p.sample(true,40240);
    assert(p.sample(true,45199)==Event::None);
    assert(p.sample(true,45200)==Event::Shutdown);
    assert(p.sample(true,45201)==Event::None);
    assert(p.sample(true,70199)==Event::None);
    assert(p.sample(true,70200)==Event::Force);
    assert(p.sample(true,90000)==Event::None);
    p.sample(false,90001);assert(!p.released());p.sample(false,90041);assert(p.released());
    p.sample(true,90100);p.sample(true,90140);assert(p.sample(true,95100)==Event::Shutdown);
    p.sample(false,120099);assert(p.sample(false,120100)==Event::None); // Release just before force.
    PowerButton wrap;wrap.sample(false,0xffff0000U);wrap.sample(false,0xffff0030U);
    wrap.sample(true,0xfffffff0U);wrap.sample(true,0x20U);
    assert(wrap.sample(true,uint32_t(0xfffffff0U+4999U))==Event::None);
    assert(wrap.sample(true,uint32_t(0xfffffff0U+5000U))==Event::Shutdown);
    assert(wrap.sample(true,uint32_t(0xfffffff0U+30000U))==Event::Force);
    std::cout<<"PASS BOOT release arming, debounce, exact 5s/30s thresholds, one event per hold, early release and clock wrap\n";
}
