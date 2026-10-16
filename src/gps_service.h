#pragma once
#include <nmea.h>
using GpsSnapshot=deck::GpsSnapshot;
class GpsService {
public:
    void configure(bool enabled);
    void suspendForAirplane(bool suspended);
    void poll();
    GpsSnapshot snapshot()const;
private:
    deck::NmeaParser parser_;
    bool preferred_=false,suspended_=false,enabled_=false,fault_=false,driver_=false,baudLocked_=false;
    uint32_t started_=0,baudAt_=0,baud_=38400;
    void apply();
};
