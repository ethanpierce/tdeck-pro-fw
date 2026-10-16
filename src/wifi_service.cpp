#include "wifi_service.h"
#include <connectivity.h>
#include <algorithm>
void WifiService::report(const String& value){status=value;if(changed)changed(value);}
bool WifiService::supported(wifi_auth_mode_t auth) {
    return auth==WIFI_AUTH_OPEN || auth==WIFI_AUTH_WPA_PSK || auth==WIFI_AUTH_WPA2_PSK || auth==WIFI_AUTH_WPA_WPA2_PSK ||
        auth==WIFI_AUTH_WPA3_PSK || auth==WIFI_AUTH_WPA2_WPA3_PSK;
}
void WifiService::configure(const WirelessOptions& options,bool reconnect) {
    if(!reconnect && enabled==bool(options.wifiEnabled) && ssid_==options.ssid && password_==options.password)return;
    enabled=options.wifiEnabled;ssid_=options.ssid;password_=options.password;
    if(scanning){WiFi.scanDelete();scanning=false;}
    connecting=connected=false;rssi=0;signalAt_=0;address="";last_=WL_NO_SHIELD;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    if(!enabled){WiFi.disconnect(true,false);WiFi.mode(WIFI_OFF);report("Off");return;}
    if(!WiFi.mode(WIFI_STA)){report("Wi-Fi could not start");return;}
    WiFi.setHostname("handheld-os");
    WiFi.disconnect(false,false);
    if(ssid_.length()) {
        WiFi.setAutoReconnect(true);
        WiFi.setMinSecurity(password_.isEmpty()?WIFI_AUTH_OPEN:WIFI_AUTH_WPA_PSK);
        WiFi.begin(ssid_.c_str(),password_.c_str());connecting=true;connectAt_=millis();report("Connecting...");
    } else report("On; select a network");
}
void WifiService::scan() {
    if(!enabled){report("Turn Wi-Fi on first");return;}
    if(scanning)return;
    WiFi.scanDelete();networks.clear();
    int result=WiFi.scanNetworks(true,false,false,160);
    scanning=result==WIFI_SCAN_RUNNING;
    if(scanning)report("Scanning 2.4 GHz networks...");else report("Wi-Fi scan could not start");
}
void WifiService::poll() {
    if(!enabled)return;
    if(scanning) {
        int n=WiFi.scanComplete();
        if(n!=WIFI_SCAN_RUNNING) {
            scanning=false;
            if(n>=0) {
                for(int i=0;i<n;++i) {
                    String ssid=WiFi.SSID(i);if(ssid.isEmpty())continue;
                    auto match=std::find_if(networks.begin(),networks.end(),[&](const WifiNetwork& row){return row.ssid==ssid && row.auth==WiFi.encryptionType(i);});
                    if(match==networks.end())networks.push_back({ssid,WiFi.RSSI(i),WiFi.encryptionType(i)});
                    else if(WiFi.RSSI(i)>match->rssi)match->rssi=WiFi.RSSI(i);
                }
                std::sort(networks.begin(),networks.end(),[](const WifiNetwork& a,const WifiNetwork& b){return a.rssi>b.rssi;});
                if(networks.size()>16)networks.resize(16);
                report(String(networks.size())+" Wi-Fi networks found");
            } else report("Wi-Fi scan failed; R retries");
            WiFi.scanDelete();
        }
    }
    auto now=WiFi.status();
    if(now!=last_) {
        last_=now;
        connected=now==WL_CONNECTED;
        if(!connected){rssi=0;address="";}
        if(connected){connecting=false;rssi=WiFi.RSSI();signalAt_=millis();address=WiFi.localIP().toString();report("Connected");}
        else if(now==WL_CONNECT_FAILED){connecting=false;address="";report("Connection failed; check password");}
        else if(now==WL_NO_SSID_AVAIL){address="";report("Saved network unavailable");}
        else if(!connecting && !scanning){address="";report("Disconnected");}
    }
    if(connecting && uint32_t(millis()-connectAt_)>=30000) {
        connecting=false;WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);report("Connection timed out; select to retry");
    }
    if(connected && uint32_t(millis()-signalAt_)>=10000) {
        const auto old=deck::wifiIndicator(enabled,connected,connecting,rssi);
        rssi=WiFi.RSSI();signalAt_=millis();
        if(old.bars!=deck::wifiIndicator(enabled,connected,connecting,rssi).bars && signalChanged)signalChanged();
    }
}
