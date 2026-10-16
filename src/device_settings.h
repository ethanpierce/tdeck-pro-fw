#pragma once
#include <Preferences.h>
#include <cstdint>
struct DeviceOptions {
    uint32_t magic=0x444b5301;
    uint8_t frontLight=0, keyboardLight=0, reserved[2]={};
    char apn[64]={}; // Empty leaves the modem's existing profile unchanged.
    uint32_t checksum=0;
};
class DeviceSettings {
public:
    void begin();
    bool save(DeviceOptions options);
    const DeviceOptions& values() const {return options_;}
    bool persistent=false;
private:
    Preferences prefs_;
    DeviceOptions options_{};
};
