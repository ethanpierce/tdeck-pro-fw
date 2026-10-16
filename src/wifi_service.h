#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "device_settings.h"
#include <functional>
#include <vector>
struct WifiNetwork {String ssid;int rssi=0;wifi_auth_mode_t auth=WIFI_AUTH_OPEN;};
class WifiService {
public:
    void configure(const WirelessOptions& options,bool reconnect=false);
    void scan();
    void poll();
    bool enabled=false,scanning=false,connecting=false,connected=false;
    int rssi=0;
    String status="Off",address;
    std::vector<WifiNetwork> networks;
    std::function<void(const String&)> changed;
    std::function<void()> signalChanged;
    static bool supported(wifi_auth_mode_t auth);
private:
    String ssid_,password_;
    uint32_t connectAt_=0,signalAt_=0;
    wl_status_t last_=WL_NO_SHIELD;
    void report(const String& value);
};
