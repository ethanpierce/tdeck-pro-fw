#include "device_settings.h"
#include <apn.h>
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
}
void DeviceSettings::begin() {
    options_=DeviceOptions{};
    persistent=prefs_.begin("decksettings",false);
    if(!persistent || prefs_.getBytesLength("options")!=sizeof(DeviceOptions)) return;
    DeviceOptions saved{};
    if(prefs_.getBytes("options",&saved,sizeof(saved))==sizeof(saved) && valid(saved) &&
        saved.checksum==checksum(saved)) options_=saved;
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
