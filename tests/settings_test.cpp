#include "device_settings.h"
#include <apn.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    for(const auto& value:{"fast.t-mobile.com","internet","a-b.c1","123.example"}) assert(deck::validApn(value));
    for(const auto& value:{"",".apn","apn.","a..b","-apn","apn-","a-.b","a.-b",
                           "a b","a/b","a_b","apn\";ATD123;","apn\r\nAT+CFUN=0"}) assert(!deck::validApn(value));
    assert(deck::validApn(std::string(63,'a')));
    assert(!deck::validApn(std::string(64,'a')));
    fakeNvs::clear();
    fakeNvs::blobs["deckos/history"]={1,2,3};
    DeviceSettings original;
    original.begin();
    assert(original.persistent && !original.values().apn[0]);
    assert(!original.values().frontLight && !original.values().keyboardLight);
    auto value=original.values();
    value.frontLight=1;value.keyboardLight=1;
    std::strcpy(value.apn,"fast.t-mobile.com");
    assert(original.save(value));
    DeviceSettings reboot;
    reboot.begin();
    assert(std::string(reboot.values().apn)=="fast.t-mobile.com");
    assert(reboot.values().frontLight && reboot.values().keyboardLight);
    assert(fakeNvs::blobs["deckos/history"]==std::vector<uint8_t>({1,2,3}));
    auto invalid=value;
    invalid.frontLight=2;
    assert(!reboot.save(invalid));
    std::strcpy(invalid.apn,"bad\"APN");invalid.frontLight=0;
    assert(!reboot.save(invalid));
    fakeNvs::failWrites=true;
    value.keyboardLight=0;
    assert(!reboot.save(value));
    assert(reboot.values().keyboardLight && !reboot.persistent);
    fakeNvs::failWrites=false;
    DeviceSettings afterFailedWrite;
    afterFailedWrite.begin();
    assert(afterFailedWrite.values().keyboardLight);
    auto wireless=afterFailedWrite.wireless();
    assert(!wireless.wifiEnabled && !wireless.bleEnabled);
    wireless.wifiEnabled=wireless.bleEnabled=1;
    std::strcpy(wireless.ssid,"Test network");std::strcpy(wireless.password,"test-only-password");
    std::strcpy(wireless.keyboardAddress,"12:34:56:78:9a:bc");wireless.keyboardAddressType=1;
    assert(afterFailedWrite.saveWireless(wireless));
    DeviceSettings wirelessRestart;wirelessRestart.begin();
    assert(wirelessRestart.wireless().bleEnabled && std::string(wirelessRestart.wireless().ssid)=="Test network");
    assert(std::string(wirelessRestart.values().apn)=="fast.t-mobile.com");
    assert(wirelessRestart.values().keyboardLight); // New settings preserve the old layout and values.
    assert(!wirelessRestart.radios().airplane && !wirelessRestart.radios().gpsEnabled && !wirelessRestart.mesh().enabled);
    const auto credentials=fakeNvs::blobs["decksettings/wireless"];
    const auto lighting=fakeNvs::blobs["decksettings/options"];
    auto radios=wirelessRestart.radios();radios.airplane=1;radios.gpsEnabled=1;
    assert(wirelessRestart.saveRadios(radios));
    DeviceSettings radioRestart;radioRestart.begin();
    assert(radioRestart.radios().airplane && radioRestart.radios().gpsEnabled);
    assert(fakeNvs::blobs["decksettings/wireless"]==credentials && fakeNvs::blobs["decksettings/options"]==lighting);
    auto mesh=radioRestart.mesh();mesh.enabled=1;mesh.region=deck::MeshRegion::US;
    assert(radioRestart.saveMesh(mesh));
    DeviceSettings meshRestart;meshRestart.begin();assert(meshRestart.mesh().enabled && meshRestart.mesh().region==deck::MeshRegion::US);
    assert(meshRestart.radios().airplane && meshRestart.wireless().bleEnabled);
    mesh.hopLimit=99;assert(!meshRestart.saveMesh(mesh));
    fakeNvs::blobs["decksettings/mesh"][5]^=1;
    DeviceSettings badMesh;badMesh.begin();assert(!badMesh.mesh().enabled && badMesh.radios().airplane);
    fakeNvs::blobs["decksettings/radios"][4]^=1;
    DeviceSettings badRadio;badRadio.begin();assert(badRadio.radios().airplane && !badRadio.radios().gpsEnabled);
    fakeNvs::blobs["decksettings/radios"].pop_back();
    DeviceSettings truncatedRadio;truncatedRadio.begin();assert(truncatedRadio.radios().airplane);
    fakeNvs::blobs["decksettings/radios"].clear();
    DeviceSettings emptyRadio;emptyRadio.begin();assert(emptyRadio.radios().airplane);
    fakeNvs::failWrites=true;wireless.wifiEnabled=0;
    assert(!wirelessRestart.saveWireless(wireless) && wirelessRestart.wireless().wifiEnabled);
    fakeNvs::failWrites=false;
    fakeNvs::blobs["decksettings/wireless"][12]^=1;
    DeviceSettings badWireless;badWireless.begin();assert(!badWireless.wireless().ssid[0] && badWireless.values().keyboardLight);
    fakeNvs::blobs["decksettings/options"][8]^=1;
    DeviceSettings corrupt;
    corrupt.begin();
    assert(!corrupt.values().apn[0] && !corrupt.values().frontLight);
    fakeNvs::blobs["decksettings/options"].pop_back();
    DeviceSettings truncated;
    truncated.begin();
    assert(!truncated.values().apn[0]);
    std::cout<<"PASS APN validation, saved settings across restart, isolated namespace, failed-write rollback, corrupt/truncated recovery\n";
}
