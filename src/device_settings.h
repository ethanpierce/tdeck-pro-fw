#pragma once
#include "nvs_store.h"
#include <cstdint>
#include <mesh_protocol.h>
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
// Separate blob: adding radio policy never changes existing credentials or lights.
struct RadioOptions {
    uint32_t magic=0x444b5201;
    uint8_t airplane=0, gpsEnabled=0, reserved[2]={};
    uint32_t checksum=0;
};
class DeviceSettings {
public:
    void begin();
    bool save(DeviceOptions options);
    const DeviceOptions& values() const {return options_;}
    const WirelessOptions& wireless() const {return wireless_;}
    bool saveWireless(WirelessOptions options);
    const RadioOptions& radios() const {return radios_;}
    bool saveRadios(RadioOptions options);
    const deck::MeshOptions& mesh() const {return mesh_;}
    bool saveMesh(const deck::MeshOptions& options);
    bool persistent=false;
private:
    NvsStore prefs_;
    DeviceOptions options_{};
    WirelessOptions wireless_{};
    RadioOptions radios_{};
    deck::MeshOptions mesh_{};
};

static_assert(sizeof(DeviceOptions)==76 && sizeof(WirelessOptions)==128, "Keep the existing NVS settings layouts");
static_assert(sizeof(RadioOptions)==12, "Keep the radio policy blob stable");
