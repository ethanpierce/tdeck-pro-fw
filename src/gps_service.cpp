#include "gps_service.h"
#include "board.h"
#include <platform.h>
#include <algorithm>
#include <driver/uart.h>
namespace {constexpr uart_port_t GpsPort=UART_NUM_2;}
void GpsService::configure(bool enabled){preferred_=enabled;apply();}
void GpsService::suspendForAirplane(bool suspended){suspended_=suspended;apply();}
void GpsService::apply(){
    const bool wanted=preferred_&&!suspended_&&!board::unsupported;
    if(wanted==enabled_)return;
    if(!wanted){
        platform::gpioOutput(board::GpsPower,false);
        enabled_=false;
        if(driver_){uart_driver_delete(GpsPort);driver_=false;}
        platform::gpioInput(board::GpsEspRx);platform::gpioInput(board::GpsEspTx);
        parser_.reset();baudLocked_=false;return;
    }
    parser_.reset();fault_=false;baud_=38400;baudLocked_=false;
    // Receive only. No UBX/NMEA configuration, backup reset, or UART forwarding.
    platform::gpioInput(board::GpsEspTx);
    uart_config_t config{};config.baud_rate=int(baud_);config.data_bits=UART_DATA_8_BITS;
    config.parity=UART_PARITY_DISABLE;config.stop_bits=UART_STOP_BITS_1;config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;config.source_clk=UART_SCLK_DEFAULT;
    driver_=uart_driver_install(GpsPort,2048,0,0,nullptr,0)==ESP_OK;
    if(!driver_||uart_param_config(GpsPort,&config)!=ESP_OK||uart_set_pin(GpsPort,UART_PIN_NO_CHANGE,board::GpsEspRx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK){
        if(driver_)uart_driver_delete(GpsPort);
        driver_=false;fault_=true;platform::gpioOutput(board::GpsPower,false);platform::gpioInput(board::GpsEspRx);return;
    }
    platform::gpioOutput(board::GpsPower,true);enabled_=true;started_=baudAt_=platform::millis();
}
void GpsService::poll(){
    if(!enabled_||suspended_)return;
    const uint32_t now=platform::millis();uint8_t bytes[256];size_t remaining=1024;
    while(remaining){
        int count=uart_read_bytes(GpsPort,bytes,std::min(remaining,sizeof(bytes)),0);
        if(count<=0)break;
        remaining-=size_t(count);
        for(int n=0;n<count;++n)if(parser_.feed(char(bytes[n]),now))baudLocked_=true;
    }
    if(!baudLocked_&&uint32_t(now-baudAt_)>=8000){
        baud_=baud_==38400?9600:38400;uart_set_baudrate(GpsPort,baud_);uart_flush_input(GpsPort);parser_.reset();baudAt_=now;
    }
}
GpsSnapshot GpsService::snapshot()const{
    auto value=parser_.snapshot(platform::millis());value.enabled=enabled_;value.preferredEnabled=preferred_;value.suspended=suspended_;
    if(suspended_)value.status="Airplane mode";
    else if(fault_)value.status="GPS UART could not start";
    else if(!enabled_)value.status="Off";
    else if(value.fix)value.status="Location fixed";
    else if(value.sentenceAgeMs==UINT32_MAX)value.status=uint32_t(platform::millis()-started_)<10000?"Starting GPS...":"No GPS data";
    else if(value.sentenceAgeMs>5000)value.status="Waiting for GPS data";
    else value.status="Acquiring fix; try outdoors";
    return value;
}
