#pragma once
#include <Preferences.h>
#include <cstdint>
struct DeviceOptions {
    uint32_t magic=0x444b5301;
    uint8_t frontLight=0, keyboardLight=0, reserved[2]={};
    char apn[64]={}; // Empty leaves the modem's existing profile unchanged.
    uint32_t checksum=0;
};
struct WirelessOptions {
    uint32_t magic=0x444b5701;
    uint8_t wifiEnabled=0, bleEnabled=0, keyboardAddressType=0, reserved=0;
    char ssid[33]={}, password[65]={}, keyboardAddress[18]={};
    uint32_t checksum=0;
};
class DeviceSettings {
public:
    void begin();
    bool save(DeviceOptions options);
    const DeviceOptions& values() const {return options_;}
    const WirelessOptions& wireless() const {return wireless_;}
    bool saveWireless(WirelessOptions options);
    bool persistent=false;
private:
    Preferences prefs_;
    DeviceOptions options_{};
    WirelessOptions wireless_{};
};
