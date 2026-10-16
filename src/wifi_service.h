#pragma once
#include <esp_wifi_types.h>
#include <esp_netif.h>
#include "device_settings.h"
#include <functional>
#include <string>
#include <vector>
// Safe to query from the HTTPS worker; true only after DHCP/IP acquisition.
bool wifiConnected();
struct WifiNetwork {std::string ssid;int rssi=0;wifi_auth_mode_t auth=WIFI_AUTH_OPEN;};
class WifiService {
public:
    void configure(const WirelessOptions& options,bool reconnect=false);
    bool suspendForAirplane(bool suspended);
    bool airplaneStopped()const{return !started_;}
    void scan();
    void poll();
    bool enabled=false,scanning=false,connecting=false,connected=false;
    int rssi=0;
    std::string status="Off",address;
    std::vector<WifiNetwork> networks;
    std::function<void(const std::string&)> changed;
    std::function<void()> signalChanged;
    static bool supported(wifi_auth_mode_t auth);
private:
    std::string ssid_,password_;
    esp_netif_t* netif_=nullptr;
    uint32_t connectAt_=0,signalAt_=0,retryAt_=0;
    bool initialized_=false,started_=false,reconnectAllowed_=false,retryConnect_=false,resumeAfterScan_=false;
    bool suspended_=false;
    uint32_t suspensionRetryAt_=0;
    WirelessOptions requested_{};
    bool initialize();
    void report(const std::string& value);
};
