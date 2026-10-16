#include "wifi_service.h"
#include "platform.h"
#include <connectivity.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
namespace {
std::atomic<bool> wanted{false},hasIp{false},scanFinished{false},stationStarted{false},disconnected{false};
std::atomic<unsigned> disconnectReason{0};
void networkEvent(void*,esp_event_base_t base,int32_t id,void* data) {
    if(!wanted.load()){hasIp=false;return;}
    if(base==WIFI_EVENT) {
        if(id==WIFI_EVENT_STA_START){disconnected=false;stationStarted=true;}
        else if(id==WIFI_EVENT_SCAN_DONE)scanFinished=true;
        else if(id==WIFI_EVENT_STA_DISCONNECTED) {
            hasIp=false;
            disconnectReason=data?static_cast<wifi_event_sta_disconnected_t*>(data)->reason:0;
            disconnected=true;
        }
    } else if(base==IP_EVENT) {
        if(id==IP_EVENT_STA_GOT_IP && wanted.load())hasIp=true;
        else if(id==IP_EVENT_STA_LOST_IP)hasIp=false;
    }
}
}
bool wifiConnected(){return wanted.load() && hasIp.load();}
void WifiService::report(const std::string& value){if(status==value)return;status=value;if(changed)changed(value);}
bool WifiService::supported(wifi_auth_mode_t auth) {
    return auth==WIFI_AUTH_OPEN || auth==WIFI_AUTH_WPA_PSK || auth==WIFI_AUTH_WPA2_PSK || auth==WIFI_AUTH_WPA_WPA2_PSK ||
        auth==WIFI_AUTH_WPA3_PSK || auth==WIFI_AUTH_WPA2_WPA3_PSK;
}
bool WifiService::initialize() {
    if(initialized_)return true;
    esp_err_t result=esp_netif_init();
    if(result!=ESP_OK && result!=ESP_ERR_INVALID_STATE)return false;
    result=esp_event_loop_create_default();
    if(result!=ESP_OK && result!=ESP_ERR_INVALID_STATE)return false;
    netif_=esp_netif_create_default_wifi_sta();
    if(!netif_)return false;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();config.nvs_enable=0;
    const bool driverReady=esp_wifi_init(&config)==ESP_OK;
    if(!driverReady || esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK || esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK ||
       esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,networkEvent,nullptr)!=ESP_OK ||
       esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,networkEvent,nullptr)!=ESP_OK) {
        esp_event_handler_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,networkEvent);
        esp_event_handler_unregister(IP_EVENT,ESP_EVENT_ANY_ID,networkEvent);
        if(driverReady)esp_wifi_deinit();
        esp_netif_destroy_default_wifi(netif_);netif_=nullptr;
        return false;
    }
    esp_netif_set_hostname(netif_,"handheld-os");
    initialized_=true;return true;
}
void WifiService::configure(const WirelessOptions& options,bool reconnect) {
    requested_=options;
    if(suspended_){report("Airplane mode");return;}
    if(!reconnect && enabled==bool(options.wifiEnabled) && ssid_==options.ssid && password_==options.password)return;
    wanted=false;hasIp=false;
    if(initialized_ && scanning)esp_wifi_scan_stop();
    scanning=connecting=connected=false;rssi=0;signalAt_=0;address.clear();
    scanFinished=false;stationStarted=false;disconnected=false;
    retryConnect_=resumeAfterScan_=false;
    enabled=options.wifiEnabled;ssid_=options.ssid;password_=options.password;
    reconnectAllowed_=enabled && !ssid_.empty();
    if(!enabled) {
        if(initialized_ && started_){esp_wifi_disconnect();esp_wifi_stop();started_=false;}
        report("Off");return;
    }
    if(!initialize()){enabled=false;report("Wi-Fi could not start");return;}
    if(started_){esp_wifi_disconnect();esp_wifi_stop();started_=false;}
    wifi_config_t config{};
    std::memcpy(config.sta.ssid,ssid_.data(),std::min(ssid_.size(),sizeof(config.sta.ssid)));
    std::memcpy(config.sta.password,password_.data(),std::min(password_.size(),sizeof(config.sta.password)));
    config.sta.threshold.authmode=password_.empty()?WIFI_AUTH_OPEN:WIFI_AUTH_WPA_PSK;
    config.sta.pmf_cfg.capable=true;config.sta.pmf_cfg.required=false;
    config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    wanted=true;
    if(esp_wifi_set_config(WIFI_IF_STA,&config)!=ESP_OK || esp_wifi_start()!=ESP_OK) {
        wanted=false;enabled=false;report("Wi-Fi could not start");return;
    }
    started_=true;wanted=true;
    if(!ssid_.empty()) {
        connecting=true;connectAt_=platform::millis();retryAt_=connectAt_;retryConnect_=true;report("Connecting...");
    } else report("On; select a network");
}
bool WifiService::suspendForAirplane(bool suspended) {
    if(suspended_==suspended && (!suspended || !started_))return true;
    suspended_=suspended;
    if(!suspended_){configure(requested_,true);return !requested_.wifiEnabled || started_;}
    // Gate callbacks and the HTTPS worker before touching the driver. Queued
    // scan/disconnect/IP events must not schedule a connection while suspended.
    wanted=false;hasIp=false;scanFinished=false;stationStarted=false;disconnected=false;
    reconnectAllowed_=retryConnect_=resumeAfterScan_=false;
    if(initialized_ && scanning)esp_wifi_scan_stop();
    scanning=connecting=connected=enabled=false;rssi=0;address.clear();networks.clear();
    if(initialized_ && started_) {
        suspensionRetryAt_=platform::millis();
        esp_wifi_disconnect();
        const esp_err_t result=esp_wifi_stop();
        if(result!=ESP_OK && result!=ESP_ERR_WIFI_NOT_STARTED){report("Wi-Fi shutdown failed; retry Airplane mode");return false;}
        started_=false;
        esp_wifi_clear_ap_list();
    }
    report("Airplane mode");if(signalChanged)signalChanged();return true;
}
void WifiService::scan() {
    if(suspended_){report("Airplane mode");return;}
    if(!enabled){report("Turn Wi-Fi on first");return;}
    if(scanning)return;
    if(connecting){esp_wifi_disconnect();connecting=false;retryConnect_=false;}
    resumeAfterScan_=!connected && !ssid_.empty() && reconnectAllowed_;
    networks.clear();scanFinished=false;
    wifi_scan_config_t config{};config.scan_type=WIFI_SCAN_TYPE_ACTIVE;config.show_hidden=false;
    config.scan_time.active.min=60;config.scan_time.active.max=160;
    scanning=esp_wifi_scan_start(&config,false)==ESP_OK;
    report(scanning?"Scanning 2.4 GHz networks...":"Wi-Fi scan could not start");
    if(!scanning && resumeAfterScan_){connecting=true;connectAt_=platform::millis();retryAt_=connectAt_;retryConnect_=true;}
}
void WifiService::poll() {
    if(suspended_){
        if(started_ && uint32_t(platform::millis()-suspensionRetryAt_)>=1000)suspendForAirplane(true);
        return;
    }
    if(!enabled)return;
    const uint32_t now=platform::millis();
    if(scanning && scanFinished.exchange(false)) {
        scanning=false;uint16_t count=0;
        if(esp_wifi_scan_get_ap_num(&count)==ESP_OK) {
            count=std::min<uint16_t>(count,64);std::vector<wifi_ap_record_t> records(count);
            if(!count || esp_wifi_scan_get_ap_records(&count,records.data())==ESP_OK) {
                for(unsigned i=0;i<count;++i) {
                    const auto& ap=records[i];std::string name(reinterpret_cast<const char*>(ap.ssid),strnlen(reinterpret_cast<const char*>(ap.ssid),sizeof(ap.ssid)));
                    if(name.empty())continue;
                    auto match=std::find_if(networks.begin(),networks.end(),[&](const WifiNetwork& row){return row.ssid==name && row.auth==ap.authmode;});
                    if(match==networks.end())networks.push_back({name,ap.rssi,ap.authmode});
                    else if(ap.rssi>match->rssi)match->rssi=ap.rssi;
                }
                std::sort(networks.begin(),networks.end(),[](const WifiNetwork& a,const WifiNetwork& b){return a.rssi>b.rssi;});
                if(networks.size()>16)networks.resize(16);
                report(std::to_string(networks.size())+" Wi-Fi networks found");
            } else report("Wi-Fi scan failed; R retries");
        } else report("Wi-Fi scan failed; R retries");
        esp_wifi_clear_ap_list();
        if(resumeAfterScan_){resumeAfterScan_=false;connecting=true;connectAt_=now;retryAt_=now;retryConnect_=true;}
    }
    if(!scanning && disconnected.exchange(false)) {
        const unsigned reason=disconnectReason.load();
        if(reason==WIFI_REASON_AUTH_FAIL || reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason==WIFI_REASON_HANDSHAKE_TIMEOUT) {
            connecting=false;reconnectAllowed_=retryConnect_=false;report("Connection failed; check password");
        } else if(reconnectAllowed_ && !ssid_.empty()) {
            if(connected || !connecting)connectAt_=now;
            connecting=true;retryConnect_=true;retryAt_=now+1000;
            if(reason==WIFI_REASON_NO_AP_FOUND)report("Saved network unavailable");
        }
    }
    const bool online=wifiConnected();
    if(online!=connected) {
        connected=online;
        if(connected) {
            connecting=retryConnect_=false;signalAt_=now;
            wifi_ap_record_t ap{};if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)rssi=ap.rssi;
            esp_netif_ip_info_t ip{};
            if(esp_netif_get_ip_info(netif_,&ip)==ESP_OK){char text[20];snprintf(text,sizeof(text),IPSTR,IP2STR(&ip.ip));address=text;}
            report("Connected");
        } else {rssi=0;address.clear();if(!connecting && !scanning && reconnectAllowed_)report("Disconnected");}
        if(signalChanged)signalChanged();
    }
    if(connecting && uint32_t(now-connectAt_)>=30000) {
        connecting=false;reconnectAllowed_=retryConnect_=false;esp_wifi_disconnect();report("Connection timed out; select to retry");
    } else if(retryConnect_ && !scanning && stationStarted.load() && int32_t(now-retryAt_)>=0) {
        const auto result=esp_wifi_connect();
        retryConnect_=result!=ESP_OK;retryAt_=now+1000;
    }
    if(connected && uint32_t(now-signalAt_)>=10000) {
        const auto old=deck::wifiIndicator(enabled,connected,connecting,rssi);
        wifi_ap_record_t ap{};rssi=esp_wifi_sta_get_ap_info(&ap)==ESP_OK?ap.rssi:0;signalAt_=now;
        if(old.bars!=deck::wifiIndicator(enabled,connected,connecting,rssi).bars && signalChanged)signalChanged();
    }
}
