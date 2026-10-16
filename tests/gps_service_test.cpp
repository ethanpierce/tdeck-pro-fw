#include <platform.h>
#include <driver/uart.h>
#include "../src/gps_service.h"
#include "../src/board.h"
#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <vector>
namespace {
uint32_t now=0;bool rail=false,failInstall=false;unsigned installs=0,deletes=0;
std::deque<uint8_t> rx;std::vector<uint32_t> bauds;std::vector<int> inputs;
std::string sentence(){std::string text="GNRMC,123519,A,4807.038,N,01131.000,E,0,0,161026,,,A";unsigned c=0;for(char b:text)c^=uint8_t(b);const char hex[]="0123456789ABCDEF";return "$"+text+"*"+hex[c>>4]+hex[c&15]+"\r\n";}
}
namespace board {bool unsupported=false;}
uint32_t platform::millis(){return now;}
void platform::gpioOutput(int pin,bool value){assert(pin==board::GpsPower);rail=value;}
void platform::gpioInput(int pin,bool,bool){inputs.push_back(pin);}
int uart_driver_install(uart_port_t p,int rxSize,int txSize,int,void*,int){assert(p==2&&rxSize==2048&&txSize==0);++installs;return failInstall?-1:0;}
int uart_driver_delete(uart_port_t p){assert(p==2);++deletes;rx.clear();return 0;}
int uart_param_config(uart_port_t p,const uart_config_t* c){assert(p==2&&c->baud_rate==38400&&c->data_bits==8&&c->parity==0);bauds.push_back(c->baud_rate);return 0;}
int uart_set_pin(uart_port_t p,int tx,int receive,int rts,int cts){assert(p==2&&tx==-1&&receive==44&&rts==-1&&cts==-1);return 0;}
int uart_read_bytes(uart_port_t p,uint8_t* bytes,size_t n,unsigned timeout){assert(p==2&&!timeout);size_t count=std::min(n,rx.size());for(size_t i=0;i<count;++i){bytes[i]=rx.front();rx.pop_front();}return int(count);}
int uart_set_baudrate(uart_port_t p,uint32_t baud){assert(p==2);bauds.push_back(baud);return 0;}
int uart_flush_input(uart_port_t p){assert(p==2);rx.clear();return 0;}
int main(){
    GpsService gps;gps.poll();assert(!rail&&!gps.snapshot().enabled&&!installs);
    gps.suspendForAirplane(true);gps.configure(true);assert(!rail&&!installs&&gps.snapshot().preferredEnabled);
    gps.suspendForAirplane(false);assert(rail&&gps.snapshot().enabled&&installs==1);
    for(char c:sentence())rx.push_back(c);now=100;gps.poll();assert(gps.snapshot().fix);
    gps.suspendForAirplane(true);assert(!rail&&!gps.snapshot().enabled&&!gps.snapshot().fix&&deletes==1);
    gps.configure(false);gps.suspendForAirplane(false);gps.poll();assert(!rail&&installs==1);
    gps.configure(true);now+=8000;gps.poll();assert(bauds.back()==9600);
    for(char c:sentence())rx.push_back(c);gps.poll();now+=16000;gps.poll();assert(bauds.back()==9600);
    rx.assign(4096,'x');gps.poll();assert(rx.size()==3072); // each UI poll is bounded
    gps.configure(false);assert(!rail&&!gps.snapshot().enabled&&deletes==2);
    assert(std::find(inputs.begin(),inputs.end(),43)!=inputs.end()&&std::find(inputs.begin(),inputs.end(),44)!=inputs.end());
    failInstall=true;gps.configure(true);assert(!rail&&!gps.snapshot().enabled&&std::string(gps.snapshot().status)=="GPS UART could not start");
    std::cout<<"PASS GPS defaultOFF, saved preference/Airplane restore, receive-only UART, baud lock, bounded polling and failure powerOFF\n";
}
