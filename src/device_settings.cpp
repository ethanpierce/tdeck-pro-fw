#include "device_settings.h"
#include <apn.h>
#include <connectivity.h>
#include <sms_pdu.h>
#include <cstddef>
#include <string>
namespace {
uint32_t checksum(const DeviceOptions& value) {
    return deck::fingerprint(std::string(reinterpret_cast<const char*>(&value),offsetof(DeviceOptions,checksum)));
}
bool valid(const DeviceOptions& value) {
    return value.magic==0x444b5301 && value.frontLight<=1 && value.keyboardLight<=1 &&
        !value.reserved[0] && !value.reserved[1] && !value.apn[63] &&
        (!value.apn[0] || deck::validApn(value.apn));
}
uint32_t checksum(const WirelessOptions& value) {
    return deck::fingerprint(std::string(reinterpret_cast<const char*>(&value),offsetof(WirelessOptions,checksum)));
}
bool valid(const WirelessOptions& value) {
    return value.magic==0x444b5701 && value.wifiEnabled<=1 && value.bleEnabled<=1 && value.keyboardAddressType<=1 && !value.reserved &&
        !value.ssid[32] && !value.password[64] && !value.keyboardAddress[17] &&
        (value.ssid[0]?deck::validWifiCredentials(value.ssid,value.password):!value.password[0]) &&
        (!value.keyboardAddress[0] || deck::validBleAddress(value.keyboardAddress));
}
}
void DeviceSettings::begin() {
    options_=DeviceOptions{};
    wireless_=WirelessOptions{};
    persistent=prefs_.begin("decksettings",false);
    if(!persistent) return;
    DeviceOptions saved{};
    if(prefs_.getBytes("options",&saved,sizeof(saved))==sizeof(saved) && valid(saved) &&
        saved.checksum==checksum(saved)) options_=saved;
    WirelessOptions wireless{};
    if(prefs_.getBytesLength("wireless")==sizeof(wireless) && prefs_.getBytes("wireless",&wireless,sizeof(wireless))==sizeof(wireless) &&
       valid(wireless) && wireless.checksum==checksum(wireless)) wireless_=wireless;
}
bool DeviceSettings::saveWireless(WirelessOptions options) {
    if(!valid(options) || !persistent)return false;
    options.checksum=checksum(options);
    if(prefs_.putBytes("wireless",&options,sizeof(options))!=sizeof(options)){persistent=false;return false;}
    wireless_=options;return true;
}
bool DeviceSettings::save(DeviceOptions options) {
    if(!valid(options) || !persistent) return false;
    options.checksum=checksum(options);
    if(prefs_.putBytes("options",&options,sizeof(options))!=sizeof(options)) {
        persistent=false;
        return false;
    }
    options_=options;
    return true;
}
