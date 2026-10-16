#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace deck {
struct CarrierNetwork { int status=0, technology=0; std::string name, code; };
std::vector<CarrierNetwork> parseCarrierScan(const std::string& line);
bool validWifiCredentials(const std::string& ssid,const std::string& password);
bool validBleAddress(const std::string& address);
struct HidKeyboardLayout { uint8_t modifier=0, keys=2, count=6, bytes=8, bitmap=0, firstUsage=0; };
// Accept keyboard modifier + key-array or NKRO bitmap reports; reject other HID layouts.
bool keyboardLayout(const uint8_t* map,size_t size,uint8_t reportId,HidKeyboardLayout& out);
class HidKeyboardDecoder {
public:
    void configure(HidKeyboardLayout layout) {layout_=layout;previous_.clear();caps_=false;}
    std::string feed(const uint8_t* report,size_t size);
private:
    HidKeyboardLayout layout_;
    std::vector<uint8_t> previous_;
    bool caps_=false;
};
}
