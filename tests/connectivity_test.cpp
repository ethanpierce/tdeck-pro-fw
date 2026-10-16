#include <connectivity.h>
#include <cassert>
#include <iostream>
#include <random>
int main() {
    auto carriers=deck::parseCarrierScan("+COPS: (1,\"Test, LTE\",\"Test\",\"310410\",7),(3,\"Public safety\",\"PS\",\"313100\",7),,(0,1,2,3,4),(0,1,2)");
    assert(carriers.size()==2 && carriers[0].name=="Test, LTE" && carriers[0].status==1 && carriers[1].status==3);
    assert(deck::parseCarrierScan("+COPS: (9,\"Bad\",\"B\",\"310410\",7),(1,\"Bad\",\"B\",\"inject\",7)").empty());
    assert(deck::validWifiCredentials(std::string(32,'s'),std::string(63,'p')));
    assert(deck::validWifiCredentials("Open network",""));
    assert(deck::validWifiCredentials("Test",std::string(64,'a')));
    assert(!deck::validWifiCredentials("Test",std::string(64,'g')));
    assert(!deck::validWifiCredentials("", "password") && !deck::validWifiCredentials(std::string(33,'a'),"password"));
    assert(!deck::validWifiCredentials("Test","short") && !deck::validWifiCredentials(std::string("a\0b",3),"password"));
    assert(deck::validBleAddress("12:ab:34:CD:56:ef") && !deck::validBleAddress("12:ab:34:CD:56:eZ"));
    assert(!deck::validBleAddress("12-ab-34-CD-56-ef"));
    const uint8_t map[]={0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
        0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,0x95,6,0x75,8,0x15,0,0x25,0x65,
        0x05,7,0x19,0,0x29,0x65,0x81,0,0xc0};
    deck::HidKeyboardLayout layout;
    assert(deck::keyboardLayout(map,sizeof(map),1,layout) && layout.modifier==0 && layout.keys==2 && layout.bytes==8);
    assert(!deck::keyboardLayout(map,sizeof(map),2,layout));
    auto bad=std::vector<uint8_t>(map,map+sizeof(map));bad[19]=2;
    assert(!deck::keyboardLayout(bad.data(),bad.size(),1,layout));
    deck::HidKeyboardDecoder decoder;decoder.configure(layout);
    uint8_t keys[8]={0,0,4,0,0,0,0,0};
    assert(decoder.feed(keys,8)=="a" && decoder.feed(keys,8).empty());
    keys[2]=0;decoder.feed(keys,8);keys[0]=2;keys[2]=5;assert(decoder.feed(keys,8)=="B");
    keys[2]=0;decoder.feed(keys,8);keys[0]=0;keys[2]=57;decoder.feed(keys,8);
    keys[2]=0;decoder.feed(keys,8);keys[2]=6;assert(decoder.feed(keys,8)=="C");
    keys[2]=0;decoder.feed(keys,8);keys[0]=1;keys[2]=7;assert(decoder.feed(keys,8).empty());
    assert(decoder.feed(keys,7).empty());
    const uint8_t nkro[]={0x05,7,0x19,0xe0,0x29,0xe7,0x75,1,0x95,8,0x81,2,
        0x19,0,0x29,0xdf,0x75,1,0x95,0xe0,0x81,2};
    assert(deck::keyboardLayout(nkro,sizeof(nkro),0,layout) && layout.bitmap && layout.bytes==29);
    decoder.configure(layout);uint8_t bitmap[29]={};bitmap[1]=1<<4;assert(decoder.feed(bitmap,29)=="a");
    bitmap[1]=0;decoder.feed(bitmap,29);bitmap[0]=2;bitmap[1]=1<<5;assert(decoder.feed(bitmap,29)=="B");
    std::mt19937 random(13579);
    for(unsigned i=0;i<6000;++i){std::vector<uint8_t> data(random()%128);for(auto& b:data)b=random();deck::keyboardLayout(data.data(),data.size(),random()%256,layout);decoder.feed(data.data(),data.size());}
    std::cout<<"PASS carrier parsing, Wi-Fi limits, BLE addresses, HID descriptor/key decoding, 6000 malformed inputs\n";
}
