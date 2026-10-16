#include <ui_input.h>
#include <cassert>
#include <iostream>
int main() {
    deck::UiRefreshState ui;
    assert(ui.ready(0));ui.rendered(ui.version);assert(!ui.ready(1000));
    ui.changed(1000,true);assert(!ui.ready(1099) && ui.ready(1100));
    ui.rendered(ui.version);ui.changed(2000,true);ui.changed(2090,true);ui.changed(2180,true);
    assert(!ui.ready(2249) && ui.ready(2250)); // Continuous editing has a deadline.
    ui.rendered(ui.version);ui.changed(3000,true);ui.changed(3001);assert(ui.ready(3001));
    const auto drawing=ui.version;ui.changed(3002);ui.rendered(drawing);assert(ui.pending && ui.ready(3002));
    ui.rendered(ui.version);assert(!ui.pending);
    ui.changed(0xfffffff0u,true);assert(!ui.ready(0x20u) && ui.ready(0x54u));
    deck::UiKeyBuffer<64> keys;char value=0;uint32_t capturedAt=0;
    for(unsigned n=0;n<64;++n)assert(keys.push(char(n+1),n*3));
    for(unsigned n=0;n<32;++n){assert(keys.pop(value,capturedAt) && value==char(n+1) && capturedAt==n*3);}
    for(unsigned n=0;n<32;++n)assert(keys.push(char(n+65),(n+64)*3));
    for(unsigned n=32;n<96;++n){assert(keys.pop(value,capturedAt) && value==char(n+1) && capturedAt==n*3);}
    assert(!keys.pop(value,capturedAt));
    for(unsigned n=0;n<64;++n)assert(keys.push('a',1000));
    assert(!keys.push('\r',1100) && keys.overflowed() && keys.dropped()==65 && !keys.pop(value,capturedAt));
    assert(!keys.push('b',1101) && keys.dropped()==66);keys.reset();
    assert(!keys.overflowed() && keys.size()==0 && keys.push('z',1200) && keys.pop(value,capturedAt) && value=='z' && capturedAt==1200);
    assert(keys.dropped()==66);
    // A key captured early during a 730ms panel refresh already had its
    // 100ms quiet period. Apply after rendering without adding another100ms.
    ui.rendered(ui.version);assert(keys.push('a',4000));
    assert(keys.pop(value,capturedAt));ui.changed(capturedAt,true);
    assert(ui.ready(4730));
    // A key near the end still receives only its remaining quiet interval.
    ui.rendered(ui.version);assert(keys.push('b',5700));
    assert(keys.pop(value,capturedAt));ui.changed(capturedAt,true);
    assert(!ui.ready(5730) && !ui.ready(5799) && ui.ready(5800));
    // Continuous typing preserves its250ms deadline across a busy frame,
    // even when the latest captured key is too recent for the quiet deadline.
    ui.rendered(ui.version);ui.changed(6000,true);ui.changed(6090,true);ui.changed(6180,true);
    assert(!ui.ready(6249) && ui.ready(6250));
    // Capture timestamps survive a clock wrap without changing age arithmetic.
    ui.rendered(ui.version);assert(keys.push('c',0xfffffff0u));
    assert(keys.pop(value,capturedAt) && capturedAt==0xfffffff0u);ui.changed(capturedAt,true);
    assert(!ui.ready(0x53u) && ui.ready(0x54u));
    std::cout<<"PASS immediate navigation, 100ms typing/250ms burst bound, redraw generation, clock wrap, timestamped busy-key coalescing/order and fail-closed overflow\n";
}
