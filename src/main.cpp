#include <Arduino.h>
#include <Wire.h>
#include <at_engine.h>
#include <sms_pdu.h>
#include <apn.h>
#include <connectivity.h>
#include "wifi_service.h"
#include "ble_service.h"
#include "board.h"
#include "device_settings.h"
#include "keyboard.h"
#include "message_store.h"
#include "modem_task.h"
#include "radio_service.h"
#include "settings.h"
#include <algorithm>
#include <cstring>

namespace {
Keyboard keyboard;
MessageStore store;
RadioService radio;
DeviceSettings settings;
WifiService wifi;
enum class Page { Home, Messages, SmsNumber, SmsText, Phone, Radio, RadioText, Diagnostics, Detail, Settings, Apn, Carriers, Wifi, WifiSsid, WifiPassword, WifiForget, Ble, BlePair, BleForget };
Page page=Page::Home;
Page detailReturn=Page::Messages;
bool dirty=true, cellTask=false, linked=false, simReady=false, smsConfigured=false;
int network=-1, signalDbm=0, battery=-1, callState=-1;
uint32_t drawnAt=0, editedAt=0, batteryAt=0, detailId=0;
unsigned refreshes=0, inboxOffset=0;
String notice="Starting radios...", sim="Checking", operatorName="Unknown", systemInfo="Not queried";
String call="Idle", caller, input, recipient;
String serialLine;
bool serialOverflow=false;
std::vector<deck::CarrierNetwork> carriers;
bool carrierScanning=false,wifiNeedsPassword=false,bleTask=false,bleActive=false,bleScanning=false;
unsigned carrierOffset=0,wifiCursor=0,bleCursor=0;
String carrierStatus="R scans nearby carriers",wifiName,bleStatus="Off",phoneStatus="Not connected",keyboardStatus="Not connected";
struct BleRow {String name,address;uint8_t type;int rssi;bool hid;};
std::vector<BleRow> bleDevices;
deck::HidKeyboardDecoder bleKeyboard;
bool pairActive=false,forgetAllBonds=false;
BleEventKind pairMode=BleEventKind::PairConfirm;
uint16_t pairHandle=0;uint32_t pairPin=0,pairAt=0,bleStatusAt=0;
void key(char c);


const char* pageName() {
    switch(page) {
    case Page::Home:return "Home"; case Page::Messages:return "Messages";
    case Page::SmsNumber:return "SMS number"; case Page::SmsText:return "SMS text";
    case Page::Phone:return "Phone"; case Page::Radio:return "Radio";
    case Page::RadioText:return "Radio text"; case Page::Diagnostics:return "Device status";
    case Page::Detail:return "Message detail"; case Page::Settings:return "Settings";
    case Page::Apn:return "APN editor";
    case Page::Carriers:return "Carrier scan";case Page::Wifi:return "Wi-Fi";
    case Page::WifiSsid:return "Wi-Fi SSID";case Page::WifiPassword:return "Wi-Fi password";
    case Page::WifiForget:return "Forget Wi-Fi";case Page::Ble:return "Bluetooth LE";
    case Page::BlePair:return "BLE pairing";case Page::BleForget:return "Forget BLE";
    }
    return "Unknown";
}
const char* networkName() {
    switch(network) {
    case 0:return "Not registered"; case 1:return "Registered"; case 2:return "Searching";
    case 3:return "Registration denied"; case 4:return "Network unknown"; case 5:return "Roaming";
    case 6:return "Home / SMS only"; case 7:return "Roaming / SMS only";
    case 8:return "Emergency service only"; case 9:return "Home / CSFB not preferred";
    case 10:return "Roaming / CSFB not preferred"; case 11:return "Emergency service only";
    default:return "Checking network";
    }
}
const char* deliveryName(Delivery d,Medium medium) {
    switch(d) {
    case Delivery::Pending:return "Pending";
    case Delivery::Submitted:return medium==Medium::Sms?"Modem accepted":"Transmitted";
    case Delivery::Received:return "Received";
    case Delivery::Failed:return "Failed";
    default:return "Result unknown";
    }
}
void changed() {dirty=true; editedAt=millis();}
void inform(const String& text) { notice=text; Serial.println(text); changed(); }
bool action(CellActionKind kind,uint32_t id=0,const String& peer="",const String& text="") {
    if (!cellTask) {inform("Cellular task unavailable"); return false;}
    CellAction a{}; a.kind=kind; a.id=id;
    snprintf(a.peer,sizeof(a.peer),"%s",peer.c_str());
    snprintf(a.text,sizeof(a.text),"%s",text.c_str());
    if (xQueueSend(cellActions,&a,0)!=pdTRUE) {inform("Cellular busy; try again"); return false;}
    return true;
}
void printSettings() {
    const auto& value=settings.values();
    Serial.printf("Settings: %s | APN: %s | Screen light: %s | Keyboard light: %s\n",
        settings.persistent?"saved on device":"storage unavailable",
        value.apn[0]?value.apn:"modem profile unchanged",
        board::revision11?(value.frontLight?"on":"off"):"not fitted",
        value.keyboardLight?"on":"off");
}
void openSettings() {page=Page::Settings;input="";changed();printSettings();}
bool saveWireless(const WirelessOptions& value) {
    if(!settings.saveWireless(value)){inform("Wireless settings could not be saved");return false;}
    return true;
}
void carrierScan() {
    page=Page::Carriers;changed();
    if(carrierScanning)return;
    if(action(CellActionKind::CarrierScan)){carrierScanning=true;carrierStatus="Scan requested...";}
}
void openWifi(){page=Page::Wifi;input="";wifiCursor=0;changed();}
void wifiPower(bool enabled) {
    auto value=settings.wireless();value.wifiEnabled=enabled;
    if(saveWireless(value))wifi.configure(value);
    changed();
}
void scanWifi(){openWifi();if(!wifi.enabled)wifiPower(true);wifi.scan();}
void connectWifi(const String& password) {
    if((wifiNeedsPassword && password.isEmpty()) || !deck::validWifiCredentials(wifiName.c_str(),password.c_str())) {
        inform("Use 8-63 password characters or 64 hex digits");return;
    }
    auto value=settings.wireless();value.wifiEnabled=1;
    std::memset(value.ssid,0,sizeof(value.ssid));std::memset(value.password,0,sizeof(value.password));
    snprintf(value.ssid,sizeof(value.ssid),"%s",wifiName.c_str());
    snprintf(value.password,sizeof(value.password),"%s",password.c_str());
    if(saveWireless(value)){input="";wifi.configure(value,true);page=Page::Wifi;changed();}
}
void forgetWifi() {
    auto value=settings.wireless();value.wifiEnabled=0;std::memset(value.ssid,0,sizeof(value.ssid));std::memset(value.password,0,sizeof(value.password));
    if(saveWireless(value)){wifi.configure(value);inform("Saved Wi-Fi network removed");}
    openWifi();
}
void openBle(){page=Page::Ble;input="";changed();}
void blePower(bool enabled) {
    auto value=settings.wireless();value.bleEnabled=enabled;
    if(!bleTask){inform("Bluetooth task unavailable");return;}
    if(saveWireless(value)) {
        if(!bleRequest(enabled?BleCommand::Enable:BleCommand::Disable))inform("Bluetooth busy; retry shortly");
        else {bleStatus=enabled?"Starting BLE...":"Stopping BLE...";changed();}
    }
}
bool bleAction(BleCommand command,const char* address="",uint8_t type=0) {
    if(!bleActive){inform("Turn Bluetooth on first");return false;}
    if(!bleRequest(command,address,type)){inform("Bluetooth busy; retry shortly");return false;}
    return true;
}
void connectBleSelection() {
    if(bleScanning){inform("Wait for the BLE scan to finish");return;}
    if(bleCursor<bleDevices.size())bleAction(BleCommand::Connect,bleDevices[bleCursor].address.c_str(),bleDevices[bleCursor].type);
}
void cancelPair(){if(pairActive)bleRequest(BleCommand::CancelPair,"",0,0,pairHandle);pairActive=false;input="";}
void forgetBle() {
    const auto old=settings.wireless();auto value=old;std::memset(value.keyboardAddress,0,sizeof(value.keyboardAddress));
    if(saveWireless(value))bleAction(forgetAllBonds?BleCommand::ForgetAll:BleCommand::ForgetKeyboard,old.keyboardAddress,old.keyboardAddressType);
    openBle();
}
void blePoll() {
    if(!bleTask)return;
    BleEvent e{};
    while(xQueueReceive(bleEvents,&e,0)==pdTRUE) {
        switch(e.kind) {
        case BleEventKind::State:
            bleActive=e.value;bleStatus=e.text;
            if(!bleActive){bleDevices.clear();bleScanning=false;keyboardStatus=phoneStatus="Not connected";cancelPair();}
            inform(e.text);break;
        case BleEventKind::Notice:inform(e.text);break;
        case BleEventKind::ScanStart:bleDevices.clear();bleCursor=0;bleScanning=true;inform(e.text);break;
        case BleEventKind::ScanDone:
            bleScanning=false;bleCursor=0;
            std::sort(bleDevices.begin(),bleDevices.end(),[](const BleRow& a,const BleRow& b){return a.hid!=b.hid?a.hid>b.hid:a.rssi>b.rssi;});
            inform(e.text);break;
        case BleEventKind::Advertising:phoneStatus=e.text;changed();break;
        case BleEventKind::Phone:phoneStatus=e.text;changed();break;
        case BleEventKind::Keyboard:keyboardStatus=e.text;inform(e.text);break;
        case BleEventKind::RememberKeyboard: {
            auto value=settings.wireless();snprintf(value.keyboardAddress,sizeof(value.keyboardAddress),"%s",e.address);value.keyboardAddressType=e.addressType&1;
            if(!saveWireless(value))inform("Keyboard works; address could not be saved");break;
        }
        case BleEventKind::Device: {
            auto found=std::find_if(bleDevices.begin(),bleDevices.end(),[&](const BleRow& row){return row.address==e.address && row.type==e.addressType;});
            BleRow row{e.text[0]?String(e.text):String("Unnamed BLE device"),String(e.address),e.addressType,int(int16_t(e.handle)),bool(e.value)};
            if(found!=bleDevices.end())*found=row;else if(bleDevices.size()<24)bleDevices.push_back(row);
            else if(row.hid){auto other=std::find_if(bleDevices.begin(),bleDevices.end(),[](const BleRow& r){return !r.hid;});if(other!=bleDevices.end())*other=row;}
            // Keep insertion order stable while selecting a device.
            changed();break;
        }
        case BleEventKind::Layout:bleKeyboard.configure({e.bytes[0],e.bytes[1],e.bytes[2],e.bytes[3],e.bytes[4],e.bytes[5]});break;
        case BleEventKind::Hid:for(char c:bleKeyboard.feed(e.bytes,e.length))key(c);break;
        case BleEventKind::PairConfirm:case BleEventKind::PairEntry:case BleEventKind::PairDisplay:
            if(pairActive && pairHandle!=e.handle){bleRequest(BleCommand::CancelPair,"",0,0,e.handle);break;}
            pairActive=true;pairMode=e.kind;pairHandle=e.handle;pairPin=e.value;pairAt=millis();input="";page=Page::BlePair;changed();break;
        case BleEventKind::PairDone:
            if(pairActive && pairHandle==e.handle){pairActive=false;input="";if(page==Page::BlePair)page=Page::Ble;}
            inform(e.text);break;
        }
    }
    if(pairActive && uint32_t(millis()-pairAt)>60000){cancelPair();page=Page::Ble;inform("Bluetooth pairing timed out");}
    if(bleActive && uint32_t(millis()-bleStatusAt)>=5000) {
        char status[160];snprintf(status,sizeof(status),"Handheld OS 0.1\nBattery: %d%%\nLTE: %s\nWi-Fi: %s\nLoRa: %s\n",battery,networkName(),wifi.status.c_str(),radio.ready?"ready":"unavailable");
        bleRequest(BleCommand::Status,"",0,0,0,status,battery<0?0:battery);bleStatusAt=millis();
    }
}
void saveApn(const String& value) {
    if(!deck::validApn(value.c_str())) {inform("Use an APN of 1-63 letters, digits, dots or hyphens");return;}
    auto options=settings.values();
    snprintf(options.apn,sizeof(options.apn),"%s",value.c_str());
    if(!settings.save(options)) {inform("Settings could not be saved");return;}
    page=Page::Settings;
    if(action(CellActionKind::SetApn,0,"",options.apn)) inform("APN saved; waiting for modem");
    else inform("APN saved; applies after restart");
    input="";
    printSettings();
}
void saveLights(bool front,bool keyboardLight) {
    if(front && !board::revision11) {inform("Screen light is not fitted on this revision");return;}
    auto options=settings.values();
    options.frontLight=front;options.keyboardLight=keyboardLight;
    if(!settings.save(options)) {inform("Settings could not be saved");return;}
    board::lights(front,keyboardLight);
    inform("Light settings saved");printSettings();
}
void smsSend(const String& number,const String& body) {
    deck::SmsSubmit checked;
    if (!deck::encodeSms(number.c_str(),body.c_str(),checked)) {inform("Number or SMS invalid (160 GSM septets max)"); return;}
    uint32_t id=store.add(Medium::Sms,false,number.c_str(),body.c_str());
    if (!action(CellActionKind::Sms,id,number,body)) store.update(id,Delivery::Failed);
    else inform("SMS queued; waiting for modem result");
}
void radioSend(const String& body) {
    std::vector<uint8_t> checked;
    if (!deck::encodeRadio({radio.node,1,body.c_str()},checked)) {inform("Radio: use 1-160 printable ASCII characters"); return;}
    uint32_t id=store.add(Medium::Radio,false,"Broadcast",body.c_str());
    if (!radio.send(body.c_str(),id)) {store.update(id,Delivery::Failed); inform("Radio unavailable or transmitting");}
    else inform("Radio transmitting...");
}
void home() {page=Page::Home; input=""; inboxOffset=0; changed();}
void cellPoll() {
    if (!cellTask) return;
    CellEvent e{};
    while (xQueueReceive(cellEvents,&e,0)==pdTRUE) {
        switch(e.kind) {
        case CellEventKind::Link:
            linked=e.value; if(!linked){simReady=false; smsConfigured=false; network=-1; signalDbm=0;} inform(e.text); break;
        case CellEventKind::Sim:sim=e.text; simReady=e.value; changed(); break;
        case CellEventKind::SmsReady:smsConfigured=e.value; changed(); break;
        case CellEventKind::Registration:if(network!=e.value){network=e.value; changed();} break;
        case CellEventKind::Signal:if(signalDbm!=e.value){signalDbm=e.value; changed();} break;
        case CellEventKind::Operator:if(operatorName!=e.text){operatorName=e.text; changed();} break;
        case CellEventKind::System:systemInfo=e.text; changed(); break;
        case CellEventKind::Trace:Serial.println(e.text); break;
        case CellEventKind::ApnResult:inform(e.text); break;
        case CellEventKind::CarrierStart:carriers.clear();carrierOffset=0;carrierScanning=true;carrierStatus=e.text;changed();break;
        case CellEventKind::Carrier:if(carriers.size()<16)carriers.push_back({e.value,int(e.id),e.text,e.peer});changed();break;
        case CellEventKind::CarrierDone:carrierScanning=false;carrierStatus=e.text;inform(e.text);break;
        case CellEventKind::SmsIn:
            if(store.add(Medium::Sms,true,e.peer,e.text,e.id)) inform(String("SMS from ")+e.peer);
            break;
        case CellEventKind::SmsSent:store.update(e.id,Delivery::Submitted); inform(e.text); break;
        case CellEventKind::Failed:
            store.update(e.id,e.value?Delivery::Unknown:Delivery::Failed); inform(e.text); break;
        case CellEventKind::Call:
            if((e.value==4 || e.value==5) && callState<0 && !e.peer[0]) caller="";
            call=e.text; callState=e.value; if(e.peer[0]) caller=e.peer;
            if(callState==4 || callState==5){page=Page::Phone; input="";}
            inform(call); break;
        }
    }
}
void printInbox() {
    for (const auto& m:store.messages()) {
        Serial.printf("#%lu %s %s %s [%s]\n%s\n",(unsigned long)m.sequence,
            m.medium==Medium::Sms?"SMS":"LoRa",m.incoming?"from":"to",m.peer,deliveryName(m.delivery,m.medium),m.text);
    }
}
void printStatus() {
    Serial.printf("Page: %s | Keyboard: %s\n",pageName(),keyboard.ready?"ready":"unavailable");
    Serial.printf("Board: v1.%d | Battery: %d%% | Modem: %s | SIM: %s\n",board::revision11?1:0,battery,linked?"AT ready":"no response",sim.c_str());
    Serial.printf("LTE: %s | RSSI: %d (0=unknown) | SMS setup: %s\n",networkName(),signalDbm,smsConfigured?"ready":"unavailable");
    Serial.printf("Operator: %s\nCPSI: %s\nLoRa: %s %.1f MHz, node %08lX, error %d\n",operatorName.c_str(),systemInfo.c_str(),
        radio.ready?"ready":"unavailable",DECK_LORA_MHZ,(unsigned long)radio.node,radio.error);
    Serial.printf("Message storage: %s | Dropped cellular events: %lu\n",store.persistent?"NVS":"RAM only",(unsigned long)droppedCellEvents.load());
    Serial.println("US T-Mobile: A7682E lacks B2/B4/B12/B66/B71; B5 alone is not broad compatibility.");
    printSettings();
    Serial.printf("Wi-Fi: %s | IP: %s | Saved network: %s\n",wifi.status.c_str(),wifi.address.c_str(),settings.wireless().ssid[0]?"yes":"no");
    Serial.printf("BLE: %s | Phone: %s | Keyboard: %s | Dropped BLE events: %lu\n",bleStatus.c_str(),phoneStatus.c_str(),keyboardStatus.c_str(),(unsigned long)droppedBleEvents.load());
    Serial.printf("Heap: %u free / %u minimum | PSRAM: %u free\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),ESP.getFreePsram());
}
void help() {
    Serial.println("Handheld OS 0.1 | Commands (USB 115200, newline):");
    Serial.println("help | status | diag | settings | inbox | scan-sim | home | keys on | keys off");
    Serial.println("apn <name> | light on/off | keyboard-light on/off | modem <read-only AT query>");
    Serial.println("modem restart (restart cellular modem after changing SIM)");
    Serial.println("carriers | wifi | wifi on/off/scan/forget | ble | ble on/off/scan/pair/reconnect | ble connect <1..24>");
    Serial.println("sms +15551234567|Hello | dial +15551234567 | answer | hangup | lora Hello");
    Serial.println("No messages/calls are sent at boot. LoRa is plaintext; no delivery acknowledgements.");
}
void command(String line) {
    line.trim();
    if(line=="help") help();
    else if(line=="status") printStatus();
    else if(line=="home") home();
    else if(line=="settings") openSettings();
    else if(line=="carriers")carrierScan();
    else if(line=="carriers results") {
        page=Page::Carriers;changed();
        Serial.printf("Carrier scan: %s | %u results\n",carrierStatus.c_str(),unsigned(carriers.size()));
        for(const auto& row:carriers)Serial.printf("Carrier: %s | %s | state %d | RAT %d\n",row.name.c_str(),row.code.c_str(),row.status,row.technology);
    }
    else if(line=="wifi")openWifi();
    else if(line=="wifi scan")scanWifi();
    else if(line=="wifi networks") {
        for(unsigned i=0;i<wifi.networks.size();++i)Serial.printf("Wi-Fi %u: %s | %d dBm | %s\n",i+1,wifi.networks[i].ssid.c_str(),wifi.networks[i].rssi,wifi.networks[i].auth==WIFI_AUTH_OPEN?"open":"secured");
    }
    else if(line=="wifi on" || line=="wifi off")wifiPower(line=="wifi on");
    else if(line=="wifi forget")forgetWifi();
    else if(line=="ble")openBle();
    else if(line=="ble on" || line=="ble off")blePower(line=="ble on");
    else if(line=="ble scan"){openBle();bleAction(BleCommand::Scan);}
    else if(line=="ble devices") {
        for(unsigned i=0;i<bleDevices.size();++i)Serial.printf("BLE %u: %s | %d dBm | %s | %s\n",i+1,bleDevices[i].name.c_str(),bleDevices[i].rssi,bleDevices[i].hid?"HID candidate":"BLE device",bleDevices[i].address.c_str());
    }
    else if(line=="ble pair"){openBle();bleAction(BleCommand::Advertise);}
    else if(line=="ble reconnect"){const auto& v=settings.wireless();bleAction(BleCommand::Connect,v.keyboardAddress,v.keyboardAddressType);}
    else if(line.startsWith("ble connect ")){unsigned n=line.substring(12).toInt();if(n && n<=bleDevices.size()){bleCursor=n-1;connectBleSelection();}else inform("Choose a BLE scan result (1-24)");}
    else if(line.startsWith("apn ")) saveApn(line.substring(4));
    else if(line=="light on" || line=="light off") saveLights(line=="light on",settings.values().keyboardLight);
    else if(line=="keyboard-light on" || line=="keyboard-light off") saveLights(settings.values().frontLight,line=="keyboard-light on");
    else if(line=="modem restart") action(CellActionKind::Restart);
    else if(line.startsWith("modem ")) action(CellActionKind::NetworkQuery,0,"",line.substring(6));
    else if(line=="diag") {action(CellActionKind::Refresh); inform("Diagnostics requested; watch USB output");}
    else if(line=="inbox") printInbox();
    else if(line=="scan-sim") action(CellActionKind::ReadInbox);
    else if(line=="keys on") {keyboard.trace=true; inform("Raw keyboard event trace on");}
    else if(line=="keys off") keyboard.trace=false;
    else if(line=="answer") action(CellActionKind::Answer);
    else if(line=="hangup") action(CellActionKind::Hangup);
    else if(line.startsWith("dial ")) {
        String number=line.substring(5);
        if(deck::phoneNumber(number.c_str())) action(CellActionKind::Dial,0,number);
        else inform("Invalid phone number");
    } else if(line.startsWith("lora ")) radioSend(line.substring(5));
    else if(line.startsWith("sms ")) {
        int separator=line.indexOf('|',4);
        if(separator<0) inform("Syntax: sms +15551234567|Hello");
        else smsSend(line.substring(4,separator),line.substring(separator+1));
    } else if(line.length()) inform("Unknown command; type help");
}
void serialPoll() {
    while(Serial.available()) {
        char c=char(Serial.read());
        if(c=='\r') continue;
        if(c=='\n') {
            if(serialOverflow) inform("Command too long; discarded"); else command(serialLine);
            serialLine=""; serialOverflow=false;
        } else if(serialLine.length()<240 && !serialOverflow) serialLine+=c;
        else serialOverflow=true;
    }
}
std::vector<const Message*> visibleMessages(Medium medium) {
    std::vector<const Message*> rows;
    for(auto i=store.messages().rbegin();i!=store.messages().rend();++i) if(i->medium==medium) rows.push_back(&*i);
    return rows;
}
void key(char c) {
    if(!c) return;
    if(c==27){cancelPair();home();return;}
    if(page==Page::Home) {
        if(c=='m') page=Page::Messages;
        if(c=='p') page=Page::Phone;
        if(c=='l') page=Page::Radio;
        if(c=='d') page=Page::Diagnostics;
        if(c=='s') {openSettings();return;}
        input=""; inboxOffset=0; changed(); return;
    }
    if(page==Page::Settings) {
        const auto& value=settings.values();
        if(c=='a') {input=value.apn[0]?value.apn:"fast.t-mobile.com";page=Page::Apn;changed();}
        else if(c=='f') {
            if(board::revision11) saveLights(!value.frontLight,value.keyboardLight);
            else inform("Screen light is not fitted on this revision");
        } else if(c=='k') saveLights(value.frontLight,!value.keyboardLight);
        else if(c=='c')carrierScan();
        else if(c=='w')openWifi();
        else if(c=='b')openBle();
        else if(c=='d') {page=Page::Diagnostics;changed();}
        else if(c=='r') {action(CellActionKind::Refresh);inform("Refreshing modem status");}
        return;
    }
    if(page==Page::Carriers) {
        if(c=='r')carrierScan();
        if(c=='j' && carrierOffset+4<carriers.size()){carrierOffset+=4;changed();}
        if(c=='k' && carrierOffset>=4){carrierOffset-=4;changed();}
        return;
    }
    if(page==Page::Wifi) {
        if(c=='w')wifiPower(!settings.wireless().wifiEnabled);
        else if(c=='r')scanWifi();
        else if(c=='n'){input="";page=Page::WifiSsid;changed();}
        else if(c=='f'){page=Page::WifiForget;changed();}
        else if(c=='j' && wifiCursor+1<wifi.networks.size()){++wifiCursor;changed();}
        else if(c=='k' && wifiCursor){--wifiCursor;changed();}
        else if((c=='\r' || c=='\n') && wifiCursor<wifi.networks.size()) {
            const auto& row=wifi.networks[wifiCursor];
            if(!WifiService::supported(row.auth)){inform("This Wi-Fi security type is unsupported");return;}
            wifiName=row.ssid;wifiNeedsPassword=row.auth!=WIFI_AUTH_OPEN;
            if(!wifiNeedsPassword)connectWifi("");
            else {input=wifiName==settings.wireless().ssid?String(settings.wireless().password):String("");page=Page::WifiPassword;changed();}
        }
        return;
    }
    if(page==Page::WifiSsid || page==Page::WifiPassword) {
        if(c=='\b'){if(input.length())input.remove(input.length()-1);changed();}
        else if(c=='\r' || c=='\n') {
            if(page==Page::WifiSsid) {if(input.isEmpty())return;wifiName=input;input="";wifiNeedsPassword=false;page=Page::WifiPassword;changed();}
            else connectWifi(input);
        } else if(c>=32 && c<=126 && input.length()<(page==Page::WifiSsid?32:64)){input+=c;changed();}
        return;
    }
    if(page==Page::WifiForget){if(c=='y')forgetWifi();else if(c=='n')openWifi();return;}
    if(page==Page::Ble) {
        if(c=='b')blePower(!settings.wireless().bleEnabled);
        else if(c=='p')bleAction(BleCommand::Advertise);
        else if(c=='r')bleAction(BleCommand::Scan);
        else if(c=='c'){const auto& v=settings.wireless();bleAction(BleCommand::Connect,v.keyboardAddress,v.keyboardAddressType);}
        else if(c=='f' || c=='x'){forgetAllBonds=c=='x';page=Page::BleForget;changed();}
        else if(c=='j' && bleCursor+1<bleDevices.size()){++bleCursor;changed();}
        else if(c=='k' && bleCursor){--bleCursor;changed();}
        else if(c=='\r' || c=='\n')connectBleSelection();
        return;
    }
    if(page==Page::BleForget){if(c=='y')forgetBle();else if(c=='n')openBle();return;}
    if(page==Page::BlePair) {
        if(pairMode==BleEventKind::PairConfirm && (c=='y' || c=='n')) {
            bleRequest(BleCommand::Confirm,"",0,c=='y',pairHandle);if(c=='n'){pairActive=false;openBle();}else inform("Pairing confirmation sent");
        } else if(pairMode==BleEventKind::PairEntry) {
            if(c=='\b'){if(input.length())input.remove(input.length()-1);changed();}
            else if((c=='\r' || c=='\n') && input.length()==6){bleRequest(BleCommand::Passkey,"",0,input.toInt(),pairHandle);input="";inform("Passkey sent");}
            else {
                const char* digits="wersdfzxcp";const char* mapped=strchr(digits,c);if(mapped)c=mapped-digits==9?'0':char('1'+mapped-digits);
                if(c>='0' && c<='9' && input.length()<6){input+=c;changed();}
            }
        }
        return;
    }
    if(page==Page::Apn) {
        if(c=='\b') {if(input.length())input.remove(input.length()-1);changed();}
        else if(c=='\r' || c=='\n') saveApn(input);
        else if(c>=32 && c<=126 && input.length()<63) {input+=c;changed();}
        return;
    }
    if(page==Page::Diagnostics) {if(c=='r') action(CellActionKind::Refresh); if(c=='i') action(CellActionKind::ReadInbox); return;}
    if(page==Page::Detail) {page=detailReturn;changed();return;}
    if(page==Page::Messages || page==Page::Radio) {
        if(c=='n') {input="";page=page==Page::Messages?Page::SmsNumber:Page::RadioText;changed();return;}
        auto rows=visibleMessages(page==Page::Messages?Medium::Sms:Medium::Radio);
        if(c=='j' && inboxOffset+4<rows.size()){inboxOffset+=4;changed();}
        if(c=='k' && inboxOffset>=4){inboxOffset-=4;changed();}
        if(c>='1' && c<='4' && inboxOffset+size_t(c-'1')<rows.size()) {
            detailId=rows[inboxOffset+c-'1']->sequence; detailReturn=page; page=Page::Detail; changed();
        }
        return;
    }
    if(page==Page::Phone && (callState>=0 || input.isEmpty())) {
        if(c=='a' && callState>=0){action(CellActionKind::Answer);return;}
        if(c=='h' && callState>=0){action(CellActionKind::Hangup);return;}
    }
    if(c=='\b') {if(input.length())input.remove(input.length()-1);changed();return;}
    if(c=='\r' || c=='\n') {
        if(page==Page::SmsNumber) {
            if(deck::phoneNumber(input.c_str())) {recipient=input;input="";page=Page::SmsText;changed();}
            else inform("Use digits and optional leading +");
        } else if(page==Page::SmsText) {
            deck::SmsSubmit checked;
            if(deck::encodeSms(recipient.c_str(),input.c_str(),checked)){smsSend(recipient,input);page=Page::Messages;input="";inboxOffset=0;}
            else inform("Use one GSM-7 SMS (160 septets)");
        } else if(page==Page::RadioText) {
            if(input.length()){radioSend(input);page=Page::Radio;input="";inboxOffset=0;}
        } else if(page==Page::Phone) {
            if(deck::phoneNumber(input.c_str())) {caller=input;action(CellActionKind::Dial,0,input);input="";}
            else inform("Use digits and optional leading +");
        }
        changed(); return;
    }
    if(c<32 || c>126) return;
    if(page==Page::Phone || page==Page::SmsNumber) {
        // Number fields accept printed digit legends without holding Alt.
        const char* letters="wersdfzxc";
        const char* found=strchr(letters,c);
        if(found) c=char('1'+(found-letters));
        if(c=='o') c='+';
        if(c=='p') c='0';
        if(!((c>='0' && c<='9') || (c=='+' && input.isEmpty()))) return;
        if(input.length()>=16) return;
    } else if(input.length()>=160) return;
    input+=c; changed();
}
void text(int x,int y,const String& value,int scale=1) {
    auto& s=*board::screen;
    s.setTextSize(scale); s.setCursor(x,y); s.print(value);
}
String clipped(const String& s,unsigned count) {return s.length()>count?s.substring(0,count-1)+"~":s;}
void wrap(const String& value,int x,int y,int columns,int rows,int scale=1) {
    String clean;
    // Built-in font is ASCII; keep received UTF-8 in storage and USB output.
    for(unsigned i=0;i<value.length();++i) {
        unsigned char c=value[i];
        if(c=='\n' || (c>=32 && c<=126)) clean+=char(c);
        else if(c>=0xc0) clean+='?';
    }
    unsigned p=0;
    for(int row=0;row<rows && p<clean.length();++row) {
        unsigned end=std::min(p+unsigned(columns),clean.length());
        int nl=clean.indexOf('\n',p);
        if(nl>=0 && unsigned(nl)<end) end=unsigned(nl);
        text(x,y+row*10*scale,clean.substring(p,end),scale);
        p=end; if(p<clean.length() && clean[p]=='\n')++p;
    }
}
void card(int x,int y,const char* shortcut,const char* title,const char* subtitle) {
    auto& s=*board::screen;
    s.drawRoundRect(x,y,106,75,6,GxEPD_BLACK);
    text(x+9,y+8,shortcut,2); text(x+9,y+34,title); text(x+9,y+51,subtitle);
}
void list(Medium medium) {
    text(10,43,medium==Medium::Sms?"MESSAGES":"LORA / 915 MHz",2);
    auto rows=visibleMessages(medium);
    if(rows.empty()) {text(10,84,"No messages yet."); text(10,102,"N  Write a message");}
    for(unsigned n=0;n<4 && inboxOffset+n<rows.size();++n) {
        const auto& m=*rows[inboxOffset+n]; int y=77+n*42;
        text(10,y,String(n+1)+" "+(m.incoming?"< ":"> ")+clipped(m.peer,25));
        text(22,y+12,clipped(m.text,33));
        text(22,y+24,deliveryName(m.delivery,m.medium));
        board::screen->drawFastHLine(10,y+36,220,GxEPD_BLACK);
    }
    text(10,251,"N New  J/K Page  Alt+1..4 Read");
}
void draw() {
    if(!board::screen || !dirty) return;
    uint32_t now=millis();
    // Coalesce typing. Panel updates take ~700 ms; the modem has its own task.
    if(refreshes && (uint32_t(now-drawnAt)<1200 || uint32_t(now-editedAt)<250)) return;
    auto& s=*board::screen;
    if(refreshes%16==0) s.setFullWindow(); else s.setPartialWindow(0,0,240,320);
    s.firstPage();
    do {
        s.fillScreen(GxEPD_WHITE);
        text(10,8,"HANDHELD OS",2);
        text(188,13,battery>=0?String(battery)+"%":"--%");
        s.drawFastHLine(10,30,220,GxEPD_BLACK);
        if(page==Page::Home) {
            text(10,42,networkName());
            text(10,57,radio.ready?"915 MHz radio ready":"Radio unavailable");
            card(10,80,"M","Messages","SMS inbox");
            card(124,80,"P","Phone","Call controls");
            card(10,165,"L","Radio","LoRa chat");
            card(124,165,"D","Status","SIM + network");
            s.drawRoundRect(10,247,220,21,4,GxEPD_BLACK);
            text(19,254,"S  Settings");
        } else if(page==Page::Messages) list(Medium::Sms);
        else if(page==Page::Radio) list(Medium::Radio);
        else if(page==Page::SmsNumber) {
            text(10,44,"NEW MESSAGE",2); text(10,78,"To: phone number");
            wrap(input+"_",10,104,18,2,2);
            text(10,170,"W/E/R=1/2/3  S/D/F=4/5/6");
            text(10,185,"Z/X/C=7/8/9  P=0  O=+");
            text(10,220,"Enter: write message");
        } else if(page==Page::SmsText || page==Page::RadioText) {
            text(10,44,page==Page::SmsText?"WRITE SMS":"LORA BROADCAST",2);
            text(10,75,page==Page::SmsText?clipped(recipient,32):"Plaintext / no delivery receipt");
            wrap(input+"_",10,100,35,13);
            text(10,242,String(input.length())+" chars  Enter: send");
        } else if(page==Page::Phone) {
            text(10,44,"PHONE",2); wrap(call,10,78,35,3);
            text(10,114,clipped(caller,34));
            wrap(input+"_",10,145,18,2,2);
            text(10,205,"Enter: dial  A: answer  H: end");
            text(10,220,"W/E/R 1/2/3 | S/D/F 4/5/6");
            text(10,235,"Z/X/C 7/8/9 | P 0 | O +");
            text(10,250,"Voice needs carrier + audio path");
        } else if(page==Page::Diagnostics) {
            text(10,44,"DEVICE STATUS",2);
            text(10,76,String("Board v1.")+(board::revision11?"1":"0"));
            text(10,92,linked?"Modem: responding":"Modem: no response");
            text(10,108,"SIM: "+clipped(sim,28));
            text(10,124,networkName());
            text(10,140,signalDbm?String(signalDbm)+" dBm RSSI":"RSSI: unknown");
            text(10,156,smsConfigured?"SMS transport configured":"SMS transport unavailable");
            text(10,172,radio.ready?"LoRa: 915 MHz ready":"LoRa error: "+String(radio.error));
            text(10,188,store.persistent?"History: saved in NVS":"History: RAM only");
            text(10,211,"A7682E: poor US band coverage");
            text(10,227,"R Refresh  I Import SIM inbox");
            text(10,243,"USB: status / diag / help");
        } else if(page==Page::Settings) {
            const auto& value=settings.values();
            text(10,44,"SETTINGS",2);
            text(10,75,"A  APN: "+clipped(value.apn[0]?String(value.apn):String("Use modem profile"),25));
            text(10,99,"C  Scan cellular carriers");
            text(10,123,"W  Wi-Fi setup");
            text(10,147,"B  Bluetooth LE setup");
            s.drawFastHLine(10,166,220,GxEPD_BLACK);
            text(10,179,String("F  Screen light: ")+(board::revision11?(value.frontLight?"On":"Off"):"Not fitted"));
            text(10,201,String("K  Keyboard light: ")+(value.keyboardLight?"On":"Off"));
            text(10,227,"D  Device status");text(10,246,"R  Refresh cellular modem");
            text(10,261,settings.persistent?"Settings saved on device":"Settings storage unavailable");
        } else if(page==Page::Apn) {
            text(10,44,"MOBILE DATA APN",2);
            wrap(input+"_",10,84,35,3);
            text(10,132,"Enter: save and apply");
            text(10,151,"Sym: cancel and return home");
            wrap("Changing the APN does not add support for missing radio bands.",10,189,35,3);
            text(10,244,"T-Mobile: fast.t-mobile.com");
        } else if(page==Page::Carriers) {
            text(10,44,"CARRIER SCAN",2);wrap(carrierStatus,10,73,35,2);
            for(unsigned n=0;n<4 && carrierOffset+n<carriers.size();++n) {
                const auto& row=carriers[carrierOffset+n];int y=105+n*31;
                text(10,y,clipped(row.code=="313100"?String("FirstNet"):String(row.name.c_str()),34));
                const char* state=row.status==1?"Available":row.status==2?"Current":row.status==3?"Forbidden":"Unknown";
                text(10,y+12,String(row.code.c_str())+"  "+(row.technology==7?String("LTE"):String("RAT ")+row.technology)+"  "+state);
            }
            text(10,239,"R Scan again   J/K Page");text(10,255,"Scan does not switch carriers");
        } else if(page==Page::Wifi) {
            text(10,44,"WI-FI / 2.4 GHz",2);text(10,73,clipped(wifi.status,35));text(10,87,wifi.address.length()?"IP "+wifi.address:String("No IP address"));
            text(10,105,"W On/Off  R Scan  N Hidden SSID");
            unsigned offset=(wifiCursor/4)*4;
            if(wifi.networks.empty())text(10,140,wifi.enabled?"No scan results yet. R scans.":"Wi-Fi is off. W turns it on.");
            for(unsigned n=0;n<4 && offset+n<wifi.networks.size();++n) {
                const auto& row=wifi.networks[offset+n];int y=128+n*24;
                text(10,y,String(offset+n==wifiCursor?"> ":"  ")+clipped(row.ssid,31));
                text(22,y+11,String(row.rssi)+" dBm / "+(row.auth==WIFI_AUTH_OPEN?"Open":WifiService::supported(row.auth)?"Password":"Unsupported security"));
            }
            text(10,232,"J/K Select   Enter Connect");text(10,250,"F Forget saved Wi-Fi network");
        } else if(page==Page::WifiSsid || page==Page::WifiPassword) {
            bool secret=page==Page::WifiPassword;
            text(10,44,secret?"WI-FI PASSWORD":"WI-FI NAME",2);
            if(secret)wrap(wifiName,10,78,35,2);
            String visible=input;
            if(secret){visible="";for(unsigned i=0;i<input.length();++i)visible+='*';}
            wrap(visible+"_",10,110,35,3);
            text(10,164,secret?"Enter: save and connect":"Enter: continue");text(10,184,"Sym: cancel and return home");
            if(secret){text(10,216,"Blank password: open Wi-Fi only");text(10,236,"Password stays on this device");}
        } else if(page==Page::WifiForget || page==Page::BleForget) {
            text(10,44,"FORGET CONNECTION",2);
            wrap(page==Page::WifiForget?String("Remove saved Wi-Fi name and password?"):String(forgetAllBonds?"Remove all BLE bonds and the saved keyboard?":"Remove the saved BLE keyboard and its bond?"),10,90,35,5);
            text(10,176,"Y Confirm     N Cancel");
        } else if(page==Page::Ble) {
            text(10,44,"BLUETOOTH LE",2);text(10,73,clipped(bleStatus,35));text(10,87,clipped(phoneStatus,35));text(10,101,clipped(keyboardStatus,35));
            text(10,119,"B On/Off  P Phone  R Scan");text(10,132,"C Reconnect saved keyboard");
            unsigned offset=(bleCursor/3)*3;
            if(bleDevices.empty())text(10,162,bleScanning?"Scanning BLE devices...":"Put keyboard in pairing mode");
            for(unsigned n=0;n<3 && offset+n<bleDevices.size();++n) {
                const auto& row=bleDevices[offset+n];int y=151+n*25;
                text(10,y,String(offset+n==bleCursor?"> ":"  ")+clipped(row.name,31));
                text(22,y+11,String(row.rssi)+" dBm "+(row.hid?"HID / keyboard candidate":"BLE device"));
            }
            text(10,232,"J/K Select   Enter Pair keyboard");text(10,249,"F Forget keyboard  X Clear bonds");
        } else if(page==Page::BlePair) {
            text(10,44,"BLUETOOTH PAIR",2);
            if(pairMode==BleEventKind::PairConfirm) {
                text(10,83,"Compare this code on both devices");
                char pin[8];snprintf(pin,sizeof(pin),"%06lu",(unsigned long)pairPin);text(10,117,pin,3);
                text(10,169,"Y Codes match   N Reject");
            } else if(pairMode==BleEventKind::PairDisplay) {
                text(10,83,"Type this code on the other device");
                char pin[8];snprintf(pin,sizeof(pin),"%06lu",(unsigned long)pairPin);text(10,117,pin,3);
                text(10,169,"Then press Enter on that device");
            } else {text(10,83,"Enter the other device's 6-digit code");text(10,117,input+"_",3);text(10,169,"Enter: submit passkey");text(10,190,"W/E/R 1/2/3 | S/D/F 4/5/6");text(10,205,"Z/X/C 7/8/9 | P 0");}
            text(10,241,"Sym cancels. Expires in 60 seconds.");
        } else if(page==Page::Detail) {
            for(const auto& m:store.messages()) if(m.sequence==detailId) {
                text(10,44,m.medium==Medium::Sms?"SMS":"LORA",2);
                text(10,75,clipped(m.peer,35));
                text(10,91,deliveryName(m.delivery,m.medium));
                wrap(m.text,10,114,35,13);
                text(10,251,"Any key: back to inbox");
            }
        }
        s.drawFastHLine(10,273,220,GxEPD_BLACK);
        wrap(notice,10,282,36,2);
        text(10,308,"Sym: Home   Handheld OS 0.1");
    } while(s.nextPage());
    dirty=false; drawnAt=millis(); ++refreshes;
}
}
void setup() {
    Serial.begin(115200); // Boot on battery without waiting for a USB host.
    board::begin();
    if(board::unsupported) {Serial.println("T-Deck Pro MAX detected; unsupported profile. Radios disabled.");return;}
    keyboard.begin();
    store.begin();
    settings.begin();
    board::lights(settings.values().frontLight,settings.values().keyboardLight);
    battery=board::batteryPercent();
    wifi.changed=[](const String& value){inform("Wi-Fi: "+value);};
    wifi.configure(settings.wireless());
    bleTask=startBleTask();
    if(bleTask && settings.wireless().bleEnabled) {
        bleRequest(BleCommand::Enable);
        if(settings.wireless().keyboardAddress[0])bleRequest(BleCommand::Connect,settings.wireless().keyboardAddress,settings.wireless().keyboardAddressType);
    }
    radio.received=[](const deck::RadioPacket& p,int rssi){
        char peer[20]; snprintf(peer,sizeof(peer),"Node %08lX",(unsigned long)p.sender);
        std::string identity=std::to_string(p.sender)+":"+std::to_string(p.sequence);
        if(store.add(Medium::Radio,true,peer,p.text.c_str(),deck::fingerprint(identity))) inform(String(peer)+" / "+String(rssi)+" dBm");
    };
    radio.sent=[](uint32_t id,bool ok){
        store.update(id,ok?Delivery::Submitted:Delivery::Unknown);
        inform(ok?"LoRa transmitted; receipt unconfirmed":"LoRa result unknown");
    };
    radio.begin();
    cellTask=startCellTask();
    if(cellTask && settings.values().apn[0]) action(CellActionKind::SetApn,0,"",settings.values().apn);
    if(!cellTask) inform("Could not start cellular task");
    if(!keyboard.ready) inform("Keyboard unavailable; use USB commands");
    help();
    draw();
}
void loop() {
    if(board::unsupported) {delay(100);return;}
    serialPoll(); cellPoll(); wifi.poll(); blePoll();
    bool radioWasReady=radio.ready;
    radio.poll();
    if(radio.ready!=radioWasReady) inform(radio.ready?"LoRa receiver restored":"LoRa receive unavailable; retrying");
    for(unsigned i=0;i<16;++i){char c=keyboard.poll();if(!c)break;key(c);}
    if(uint32_t(millis()-batteryAt)>=30000){batteryAt=millis();int value=board::batteryPercent();if(value!=battery){battery=value;changed();}}
    draw();
    delay(5);
}
