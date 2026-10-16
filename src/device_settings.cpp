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
uint32_t checksum(const RadioOptions& value) {
    return deck::fingerprint(std::string(reinterpret_cast<const char*>(&value),offsetof(RadioOptions,checksum)));
}
bool valid(const RadioOptions& value) {
    return value.magic==0x444b5201 && value.airplane<=1 && value.gpsEnabled<=1 &&
        !value.reserved[0] && !value.reserved[1];
}
struct MeshBlob {
    uint32_t magic=0x444b4d01;
    deck::MeshOptions options{};
    uint32_t checksum=0;
};
static_assert(sizeof(MeshBlob)==108,"Keep the mesh blob layout stable");
uint32_t checksum(const MeshBlob& value) {
    return deck::fingerprint(std::string(reinterpret_cast<const char*>(&value),offsetof(MeshBlob,checksum)));
}
}
void DeviceSettings::begin() {
    options_=DeviceOptions{};
    wireless_=WirelessOptions{};
    radios_=RadioOptions{};
    mesh_=deck::MeshOptions{};
    persistent=prefs_.begin("decksettings",false);
    if(!persistent) {radios_.airplane=1;return;}
    DeviceOptions saved{};
    if(prefs_.getBytes("options",&saved,sizeof(saved))==sizeof(saved) && valid(saved) &&
        saved.checksum==checksum(saved)) options_=saved;
    WirelessOptions wireless{};
    if(prefs_.getBytesLength("wireless")==sizeof(wireless) && prefs_.getBytes("wireless",&wireless,sizeof(wireless))==sizeof(wireless) &&
       valid(wireless) && wireless.checksum==checksum(wireless)) wireless_=wireless;
    const size_t radioLength=prefs_.getBytesLength("radios");
    if(prefs_.contains("radios")) {
        RadioOptions savedRadios{};
        if(radioLength==sizeof(savedRadios) && prefs_.getBytes("radios",&savedRadios,sizeof(savedRadios))==sizeof(savedRadios) &&
           valid(savedRadios) && savedRadios.checksum==checksum(savedRadios)) radios_=savedRadios;
        else radios_.airplane=1; // Damaged policy must not unexpectedly start radios.
    }
    MeshBlob savedMesh{};
    if(prefs_.getBytesLength("mesh")==sizeof(savedMesh) && prefs_.getBytes("mesh",&savedMesh,sizeof(savedMesh))==sizeof(savedMesh) &&
       savedMesh.magic==0x444b4d01 && savedMesh.checksum==checksum(savedMesh) && deck::validMeshOptions(savedMesh.options)) mesh_=savedMesh.options;
}
bool DeviceSettings::saveMesh(const deck::MeshOptions& options) {
    if(!deck::validMeshOptions(options) || !persistent)return false;
    MeshBlob blob;blob.options=options;blob.checksum=checksum(blob);
    if(prefs_.putBytes("mesh",&blob,sizeof(blob))!=sizeof(blob)){persistent=false;return false;}
    mesh_=options;return true;
}
bool DeviceSettings::saveRadios(RadioOptions options) {
    if(!valid(options) || !persistent)return false;
    options.checksum=checksum(options);
    if(prefs_.putBytes("radios",&options,sizeof(options))!=sizeof(options)){persistent=false;return false;}
    radios_=options;return true;
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
