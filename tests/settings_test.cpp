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
