#include <platform.h>
#include "native_text.h"
#include "native_screen.h"
#include <nvs_flash.h>
#include <esp_system.h>
#include <at_engine.h>
#include <sms_pdu.h>
#include <apn.h>
#include <connectivity.h>
#include <conversations.h>
#include <ai_models.h>
#include <ui_input.h>
#include <ui_layout.h>
#include <chat_lines.h>
#include "gps_service.h"
#include "airplane_mode.h"
#include "wifi_service.h"
#include "ble_service.h"
#include "ai_chat.h"
#include "ai_settings.h"
#include "board.h"
#include "device_settings.h"
#include "keyboard.h"
#include "touch_input.h"
#include "message_store.h"
#include "modem_task.h"
#include "cellular_socket.h"
#include "radio_service.h"
#include "settings.h"
#include <algorithm>
#include <cstring>

using platform::millis;
using platform::delay;

namespace {
Keyboard keyboard;
TouchInput touch;
MessageStore store;
RadioService radio;
DeviceSettings settings;
WifiService wifi;
AiSettings aiSettings;
AiChat ai;
GpsService gps;
unsigned conversationCursor=0,threadScroll=0,modelCursor=0,settingsCursor=0,homeCursor=0;
constexpr unsigned ThreadRows=10;
enum class Page { Home, Messages, SmsNumber, SmsThread, Phone, Radio, Diagnostics, Settings, Connections, MessagingSettings, DisplaySettings, CellularSettings, MeshSettings, MeshRegion, MeshPreset, MeshChannel, MeshKey, MeshNodes, Location, Apn, Carriers, Wifi, WifiSsid, WifiPassword, WifiForget, Ble, BlePair, BleForget, Ai, AiSetup, AiModelPicker, AiEndpoint, AiModel, AiKey };
Page page=Page::Home;
Page threadReturn=Page::Messages;
Page aiSetupReturn=Page::Home;
Page meshSetupReturn=Page::MessagingSettings;
deck::UiRefreshState ui;
deck::UiKeyBuffer<64> busyKeys;
bool applyingBusyKey=false;uint32_t busyKeyTime=0;
bool cellTask=false, linked=false, simReady=false, smsConfigured=false;
int network=-1, signalDbm=0, battery=-1, callState=-1;
uint32_t batteryAt=0,lastDrawMs=0,maxDrawMs=0,busyPollAt=0;
unsigned frameQueuedKeys=0;
Page renderedPage=Page::Home;
unsigned renderedScroll=0;
unsigned refreshes=0;
Text notice="Starting radios...", sim="Checking", operatorName="Unknown", systemInfo="Not queried";
Text call="Idle", caller, input, recipient,phoneFeedback;
Text serialLine,pendingLocationText;
uint32_t locationCapturedAt=0;
Text selectedConversation="ai",aiPending,aiTransport;
bool serialOverflow=false;
uint32_t gpsUiAt=0;Text gpsUiSignature;bool gpsUiFix=false,gpsUiEnabled=false;
std::vector<deck::CarrierNetwork> carriers;
bool carrierScanning=false,wifiNeedsPassword=false,bleTask=false,bleActive=false,bleScanning=false;
unsigned carrierOffset=0,wifiCursor=0,bleCursor=0;
Text carrierStatus="R scans nearby carriers",wifiName,bleStatus="Off",phoneStatus="Not connected",keyboardStatus="Not connected";
struct BleRow {Text name,address;uint8_t type;int rssi;bool hid;};
std::vector<BleRow> bleDevices;
deck::HidKeyboardDecoder bleKeyboard;
bool pairActive=false,forgetAllBonds=false;
BleEventKind pairMode=BleEventKind::PairConfirm;
uint16_t pairHandle=0;uint32_t pairPin=0,pairAt=0,bleStatusAt=0;
void key(char c);
unsigned threadLineCount();
void touchAt(int x,int y);
void sendConversation();
bool radiosAllowed();
void inform(const Text&);
void shareLocation() {
    if(!radiosAllowed())return;
    const auto point=gps.snapshot();
    std::string link;
    if(!deck::gpsMapsLink(point,link)){inform("No fresh GPS fix. Go outdoors and try again.");return;}
    pendingLocationText=point.utc[0]?Text("Location captured at ")+Text(point.utc)+"\n"+Text(link):Text("My captured location\n")+Text(link);locationCapturedAt=millis();page=Page::SmsNumber;input="";
    inform("Choose a recipient, then review your location message.");
}



const char* pageName() {
    switch(page) {
    case Page::Home:return "Home"; case Page::Messages:return "Messages";
    case Page::SmsNumber:return "New SMS"; case Page::SmsThread:return "SMS conversation";
    case Page::Phone:return "Phone"; case Page::Radio:return "Radio";
    case Page::Diagnostics:return "Device status";case Page::Settings:return "Settings";
    case Page::Connections:return "Connections";case Page::MessagingSettings:return "Messaging settings";
    case Page::DisplaySettings:return "Display";case Page::CellularSettings:return "Cellular";
    case Page::MeshSettings:return "Meshtastic";case Page::MeshRegion:return "Mesh region";
    case Page::MeshPreset:return "Mesh preset";case Page::MeshChannel:return "Mesh channel";
    case Page::MeshKey:return "Mesh channel key";case Page::MeshNodes:return "Heard nodes";case Page::Location:return "Location";
    case Page::Apn:return "APN editor";
    case Page::Carriers:return "Carrier scan";case Page::Wifi:return "Wi-Fi";
    case Page::WifiSsid:return "Wi-Fi SSID";case Page::WifiPassword:return "Wi-Fi password";
    case Page::WifiForget:return "Forget Wi-Fi";case Page::Ble:return "Bluetooth LE";
    case Page::BlePair:return "BLE pairing";case Page::BleForget:return "Forget BLE";
    case Page::Ai:return "AI conversation";case Page::AiModelPicker:return "Choose AI model";
    case Page::AiSetup:return "AI setup";case Page::AiEndpoint:return "AI endpoint";
    case Page::AiModel:return "AI model";case Page::AiKey:return "AI API key";
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
void changed(bool typing=false) {
    ui.changed(typing && applyingBusyKey?busyKeyTime:millis(),typing);
}
void inform(const Text& text) { notice=text; platform::console.println(text); changed(); }
bool radiosAllowed() {
    if(!airplaneMode())return true;
    inform("Airplane mode is on. Turn it off in Settings.");return false;
}
bool action(CellActionKind kind,uint32_t id=0,const Text& peer="",const Text& text="") {
    if(airplaneMode() && kind!=CellActionKind::Hangup){inform("Airplane mode is on. Cellular is off.");return false;}
    if (!cellTask) {inform("Cellular task unavailable"); return false;}
    CellAction a{}; a.kind=kind; a.id=id;
    snprintf(a.peer,sizeof(a.peer),"%s",peer.c_str());
    snprintf(a.text,sizeof(a.text),"%s",text.c_str());
    if (xQueueSend(cellActions,&a,0)!=pdTRUE) {inform("Cellular busy; try again"); return false;}
    return true;
}
void printSettings() {
    const auto& value=settings.values();
    platform::console.printf("Settings: %s | APN: %s | Screen light: %s | Keyboard light: %s\n",
        settings.persistent?"saved on device":"storage unavailable",
        value.apn[0]?value.apn:"modem profile unchanged",
        board::revision11?(value.frontLight?"on":"off"):"not fitted",
        value.keyboardLight?"on":"off");
}
void openSettings() {page=Page::Settings;settingsCursor=0;input="";notice="";changed();printSettings();}
void openCategory(Page target){page=target;settingsCursor=0;input="";notice="";changed();}
bool inConversation(){return page==Page::SmsThread || page==Page::Radio || page==Page::Ai;}
void openMessages(){page=Page::Messages;input="";notice="";threadScroll=0;changed();}
void openAi(Page back=Page::Home){threadReturn=back;page=Page::Ai;input="";threadScroll=0;changed();}
void openRadio(Page back=Page::Home){threadReturn=back;page=Page::Radio;input="";notice=radio.status();threadScroll=0;changed();}
void openSms(const Text& peer){threadReturn=Page::Messages;recipient=peer;page=Page::SmsThread;input="";notice=linked && simReady?"":"Check cellular service before sending";threadScroll=0;changed();}
void openAiSetup(Page back=Page::Home){aiSetupReturn=back;page=Page::AiSetup;input="";changed();}
Text aiConnectionText() {
    Text connection=ai.busy && aiTransport.length()?Text("AI over ")+aiTransport:wifi.connected?Text("Wi-Fi connected"):cellTcpReady()?Text("LTE ready"):Text("No data connection");
    return connection+(aiSettings.values().key[0]?" / key saved":" / no key");
}
Text aiStartHint() {
    const auto& value=aiSettings.values();
    if(airplaneMode())return "Airplane mode is on. Turn it off in Settings.";
    if(!value.model[0])return "Choose a model in AI settings.";
    const auto url=deck::aiCompletionUrl(value.endpoint);
    const bool openAi=url.rfind("https://api.openai.com/",0)==0 || url.rfind("https://api.openai.com:443/",0)==0;
    if(openAi && !value.key[0])return "Add your API key in AI settings.";
    if(!wifi.connected && !cellTcpReady())return "Connect Wi-Fi or check cellular service to chat.";
    return "Ready. Type a message below.";
}
Text aiScreenStatus() {
    return ai.status=="Set endpoint, model and key in Settings > I"?aiStartHint():ai.status;
}
bool configureAi(AiSettings::Field field,const Text& value,const char* marker) {
    if(field==AiSettings::Field::Key)keyboard.trace=false;
    if(ai.busy){inform("AI setup busy; wait for the request");return false;}
    if(!aiSettings.set(field,value.c_str())){inform("AI setup not saved; check the field or storage");return false;}
    ai.clear();threadScroll=0;openAiSetup(aiSetupReturn);inform(marker);return true;
}
void saveAiField() {
    const auto field=page==Page::AiEndpoint?AiSettings::Field::Endpoint:page==Page::AiModel?AiSettings::Field::Model:AiSettings::Field::Key;
    const char* marker=field==AiSettings::Field::Endpoint?"AI endpoint saved":field==AiSettings::Field::Model?"AI model saved":"AI key saved";
    configureAi(field,input,marker);
}
void openAiModelPicker() {
    modelCursor=deck::AiModelPresetCount;
    for(size_t n=0;n<deck::AiModelPresetCount;++n)if(!std::strcmp(aiSettings.values().model,deck::AiModelPresets[n].id)){modelCursor=n;break;}
    page=Page::AiModelPicker;input="";changed();
}
bool saveWireless(const WirelessOptions& value) {
    if(!settings.saveWireless(value)){inform("Wireless settings could not be saved");return false;}
    return true;
}
void carrierScan() {
    page=Page::Carriers;changed();
    if(carrierScanning || !radiosAllowed())return;
    if(action(CellActionKind::CarrierScan)){carrierScanning=true;carrierStatus="Scan requested...";}
}
void openWifi(){page=Page::Wifi;input="";wifiCursor=0;changed();}
void wifiPower(bool enabled) {
    if(!radiosAllowed())return;
    auto value=settings.wireless();value.wifiEnabled=enabled;
    if(saveWireless(value))wifi.configure(value);
    changed();
}
void scanWifi(){openWifi();if(!radiosAllowed())return;if(!wifi.enabled)wifiPower(true);wifi.scan();}
void connectWifi(const Text& password) {
    if(!radiosAllowed())return;
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
    if(!radiosAllowed())return;
    auto value=settings.wireless();value.bleEnabled=enabled;
    if(!bleTask){inform("Bluetooth task unavailable");return;}
    if(saveWireless(value)) {
        if(!bleRequest(enabled?BleCommand::Enable:BleCommand::Disable))inform("Bluetooth busy; retry shortly");
        else {bleStatus=enabled?"Starting BLE...":"Stopping BLE...";changed();}
    }
}
bool bleAction(BleCommand command,const char* address="",uint8_t type=0) {
    if(!radiosAllowed())return false;
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
    if(!radiosAllowed())return;
    if(!bleActive){inform("Turn Bluetooth on to forget pairing");return;}
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
            if(!saveWireless(value))inform("Keyboard works; address could not be saved");
            break;
        }
        case BleEventKind::Device: {
            auto found=std::find_if(bleDevices.begin(),bleDevices.end(),[&](const BleRow& row){return row.address==e.address && row.type==e.addressType;});
            BleRow row{e.text[0]?Text(e.text):Text("Unnamed BLE device"),Text(e.address),e.addressType,int(int16_t(e.handle)),bool(e.value)};
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
void saveApn(const Text& value) {
    if(!deck::validApn(value.c_str())) {inform("Use an APN of 1-63 letters, digits, dots or hyphens");return;}
    auto options=settings.values();
    snprintf(options.apn,sizeof(options.apn),"%s",value.c_str());
    if(!settings.save(options)) {inform("Settings could not be saved");return;}
    page=Page::CellularSettings;
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
void smsSend(const Text& number,const Text& body) {
    if(!radiosAllowed())return;
    deck::SmsSubmit checked;
    if (!deck::encodeSms(number.c_str(),body.c_str(),checked)) {inform("Number or SMS invalid (160 GSM septets max)"); return;}
    uint32_t id=store.add(Medium::Sms,false,number.c_str(),body.c_str());
    if (!action(CellActionKind::Sms,id,number,body)) store.update(id,Delivery::Failed);
    else inform("SMS queued; waiting for modem result");
}
void radioSend(const Text& body) {
    if(!radiosAllowed())return;
    if(!radio.ready){inform(radio.status());return;}
    if(body.empty() || body.size()>deck::MeshTextLimit){inform("Use 1-160 characters for this channel");return;}
    uint32_t id=store.add(Medium::Radio,false,settings.mesh().channel,body.c_str());
    if(!radio.send(body,id)){store.update(id,Delivery::Failed);inform(radio.status());}
    else inform("Sending to the channel...");
}

bool saveMesh(const deck::MeshOptions& options) {
    if(!radiosAllowed())return false;
    if(!deck::validMeshOptions(options)){inform("Check the channel settings");return false;}
    if(!settings.saveMesh(options)){inform("Mesh settings could not be saved");return false;}
    radio.configure(options);page=Page::MeshSettings;settingsCursor=0;input="";inform(radio.status());return true;
}
void usePublicMeshKey() {
    auto options=settings.mesh();options.keyBytes=16;std::memset(options.psk,0,sizeof(options.psk));std::memcpy(options.psk,deck::MeshDefaultKey,16);saveMesh(options);
}
void home() {page=Page::Home;input="";threadScroll=0;changed();}
void cellPoll() {
    if (!cellTask) return;
    CellEvent e{};
    while (xQueueReceive(cellEvents,&e,0)==pdTRUE) {
        switch(e.kind) {
        case CellEventKind::Link:
            linked=e.value; if(!linked){simReady=false; smsConfigured=false; network=-1; signalDbm=0;operatorName="Unknown";systemInfo="Not queried";} inform(e.text); break;
        case CellEventKind::Sim:sim=e.text; simReady=e.value; changed(); break;
        case CellEventKind::SmsReady:smsConfigured=e.value; changed(); break;
        case CellEventKind::Registration:if(network!=e.value){network=e.value; changed();} break;
        case CellEventKind::Signal:if(signalDbm!=e.value){signalDbm=e.value; changed();} break;
        case CellEventKind::Operator:if(operatorName!=e.text){operatorName=e.text; changed();} break;
        case CellEventKind::System:systemInfo=e.text; changed(); break;
        case CellEventKind::Trace:platform::console.println(e.text); break;
        case CellEventKind::ApnResult:inform(e.text); break;
        case CellEventKind::CarrierStart:carriers.clear();carrierOffset=0;carrierScanning=true;carrierStatus=e.text;changed();break;
        case CellEventKind::Carrier:if(carriers.size()<16)carriers.push_back({e.value,int(e.id),e.text,e.peer});changed();break;
        case CellEventKind::CarrierDone:carrierScanning=false;carrierStatus=e.text;inform(e.text);break;
        case CellEventKind::SmsIn:
            if(store.add(Medium::Sms,true,e.peer,e.text,e.id)) inform(Text("SMS from ")+e.peer);
            break;
        case CellEventKind::SmsSent:store.update(e.id,Delivery::Submitted); inform(e.text); break;
        case CellEventKind::Failed:
            store.update(e.id,e.value?Delivery::Unknown:Delivery::Failed); inform(e.text); break;
        case CellEventKind::Call:
            if((e.value==4 || e.value==5) && callState<0 && !e.peer[0]) caller="";
            call=e.text;phoneFeedback=e.text; callState=e.value; if(e.peer[0]) caller=e.peer;
            if(callState==4 || callState==5){page=Page::Phone; input="";}
            inform(call); break;
        }
    }
}
void printInbox() {
    for (const auto& m:store.messages()) {
        platform::console.printf("#%lu %s %s %s [%s]\n%s\n",(unsigned long)m.sequence,
            m.medium==Medium::Sms?"SMS":"LoRa",m.incoming?"from":"to",m.peer,deliveryName(m.delivery,m.medium),m.text);
    }
}
void printStatus() {
    platform::console.printf("Runtime: native ESP-IDF %s | Entry: app_main\n",esp_get_idf_version());
    platform::console.printf("Page: %s | Keyboard: %s\n",pageName(),keyboard.ready?"ready":"unavailable");
    platform::console.printf("Touch: %s / %s\n",touch.ready?"ready":"unavailable",touch.model());
    platform::console.printf("Board: v1.%d | Battery: %d%% | Modem: %s | SIM: %s\n",board::revision11?1:0,battery,linked?"AT ready":"no response",sim.c_str());
    platform::console.printf("LTE: %s | RSSI: %d (0=unknown) | SMS setup: %s\n",networkName(),signalDbm,smsConfigured?"ready":"unavailable");
    platform::console.printf("Operator: %s\nCPSI: %s\nLoRa: %s %.1f MHz, node %08lX, error %d\n",operatorName.c_str(),systemInfo.c_str(),
        radio.ready?"ready":"unavailable",DECK_LORA_MHZ,(unsigned long)radio.node,radio.error);
    platform::console.printf("Message storage: %s | Dropped cellular events: %lu\n",store.persistent?"NVS":"RAM only",(unsigned long)droppedCellEvents.load());
    platform::console.println("US T-Mobile: A7682E lacks B2/B4/B12/B66/B71; B5 alone is not broad compatibility.");
    printSettings();
    platform::console.printf("Wi-Fi: %s | IP: %s | Saved network: %s\n",wifi.status.c_str(),wifi.address.c_str(),settings.wireless().ssid[0]?"yes":"no");
    platform::console.printf("BLE: %s | Phone: %s | Keyboard: %s | Dropped BLE events: %lu\n",bleStatus.c_str(),phoneStatus.c_str(),keyboardStatus.c_str(),(unsigned long)droppedBleEvents.load());
    platform::console.printf("Heap: %u free / %u minimum | PSRAM: %u free\n",unsigned(platform::freeHeap()),unsigned(platform::minimumFreeHeap()),unsigned(platform::freePsram()));
    platform::console.printf("AI: %s | Key: %s | Setup storage: %s\n",ai.status.c_str(),aiSettings.values().key[0]?"saved (hidden)":"not set",aiSettings.persistent?"NVS":"unavailable");
    platform::console.printf("Display: %lu ms last / %lu ms maximum | Keys buffered during last frame: %u | Dropped keys: %lu\n",
        (unsigned long)lastDrawMs,(unsigned long)maxDrawMs,frameQueuedKeys,(unsigned long)busyKeys.dropped());
}
void help() {
    platform::console.println("Handheld OS 0.1 | Commands (USB 115200, newline):");
    platform::console.println("help | status | diag | settings | inbox | scan-sim | home | keys on | keys off");
    platform::console.println("apn <name> | light on/off | keyboard-light on/off | modem <read-only AT query>");
    platform::console.println("modem restart (restart cellular modem after changing SIM)");
    platform::console.println("modem auto (request automatic carrier registration; up to 3 minutes)");
    platform::console.println("carriers | wifi | wifi on/off/scan/forget | ble | ble on/off/scan/pair/reconnect | ble connect <1..24>");
    platform::console.println("ai | ai setup | ai clear | ai endpoint <https-base> | ai model <id> | ai key <secret> | ai clear-key");
    platform::console.println("ai ask <message> (one real provider request using the saved setup)");
    platform::console.println("airplane on | airplane off | airplane status");
    platform::console.println("sms +15551234567|Hello | dial +15551234567 | answer | hangup | lora Hello");
    platform::console.println("No messages/calls are sent at boot. LoRa is plaintext; no delivery acknowledgements.");
}
void command(Text line) {
    line.trim();
    if(line=="help") help();
    else if(line=="airplane status")platform::console.println(airplaneStatus());
    else if(line=="airplane on" || line=="airplane off") {
        setAirplaneMode(line=="airplane on");inform(airplaneStatus());
    }
    else if(line=="status") printStatus();
    else if(line=="home") home();
    else if(line=="messages")openMessages();
    else if(line=="lora")openRadio();
    else if(line=="settings") openSettings();
    else if(line=="ai")openAi();
    else if(line.startsWith("ai ask ")) {
        if(ai.busy){inform("Wait for the current AI request");return;}
        const Text prompt=line.substring(7);
        if(!prompt.length() || prompt.length()>deck::AiPromptLimit){inform("Use 1-384 characters for AI");return;}
        openAi();input=prompt;sendConversation();
    }
    else if(line=="ai setup" || line=="ai settings")openAiSetup();
    else if(line=="ai clear"){ai.clear();threadScroll=0;changed();}
    else if(line.startsWith("ai endpoint "))configureAi(AiSettings::Field::Endpoint,line.substring(12),"AI endpoint saved");
    else if(line.startsWith("ai model "))configureAi(AiSettings::Field::Model,line.substring(9),"AI model saved");
    else if(line.startsWith("ai key "))configureAi(AiSettings::Field::Key,line.substring(7),"AI key saved");
    else if(line=="ai clear-key")configureAi(AiSettings::Field::Key,"","AI key removed");
    else if(line=="carriers")carrierScan();
    else if(line=="carriers results") {
        page=Page::Carriers;changed();
        platform::console.printf("Carrier scan: %s | %u results\n",carrierStatus.c_str(),unsigned(carriers.size()));
        for(const auto& row:carriers)platform::console.printf("Carrier: %s | %s | state %d | RAT %d\n",row.name.c_str(),row.code.c_str(),row.status,row.technology);
    }
    else if(line=="wifi")openWifi();
    else if(line=="wifi scan")scanWifi();
    else if(line=="wifi networks") {
        for(unsigned i=0;i<wifi.networks.size();++i)platform::console.printf("Wi-Fi %u: %s | %d dBm | %s\n",i+1,wifi.networks[i].ssid.c_str(),wifi.networks[i].rssi,wifi.networks[i].auth==WIFI_AUTH_OPEN?"open":"secured");
    }
    else if(line=="wifi on" || line=="wifi off")wifiPower(line=="wifi on");
    else if(line=="wifi forget")forgetWifi();
    else if(line=="ble")openBle();
    else if(line=="ble on" || line=="ble off")blePower(line=="ble on");
    else if(line=="ble scan"){openBle();bleAction(BleCommand::Scan);}
    else if(line=="ble devices") {
        for(unsigned i=0;i<bleDevices.size();++i)platform::console.printf("BLE %u: %s | %d dBm | %s | %s\n",i+1,bleDevices[i].name.c_str(),bleDevices[i].rssi,bleDevices[i].hid?"HID candidate":"BLE device",bleDevices[i].address.c_str());
    }
    else if(line=="ble pair"){openBle();bleAction(BleCommand::Advertise);}
    else if(line=="ble reconnect"){const auto& v=settings.wireless();bleAction(BleCommand::Connect,v.keyboardAddress,v.keyboardAddressType);}
    else if(line.startsWith("ble connect ")){unsigned n=line.substring(12).toInt();if(n && n<=bleDevices.size()){bleCursor=n-1;connectBleSelection();}else inform("Choose a BLE scan result (1-24)");}
    else if(line.startsWith("apn ")) saveApn(line.substring(4));
    else if(line=="light on" || line=="light off") saveLights(line=="light on",settings.values().keyboardLight);
    else if(line=="keyboard-light on" || line=="keyboard-light off") saveLights(settings.values().frontLight,line=="keyboard-light on");
    else if(line=="modem restart") action(CellActionKind::Restart);
    else if(line=="modem auto") action(CellActionKind::SelectAutomatic);
    else if(line.startsWith("modem ")) action(CellActionKind::NetworkQuery,0,"",line.substring(6));
    else if(line=="diag") {action(CellActionKind::Refresh); inform("Diagnostics requested; watch USB output");}
    else if(line=="inbox") printInbox();
    else if(line=="scan-sim") action(CellActionKind::ReadInbox);
    else if(line=="keys on") {if(page==Page::AiKey || page==Page::MeshKey || page==Page::WifiPassword){keyboard.trace=false;inform("Keyboard trace unavailable during secret entry");}else{keyboard.trace=true;inform("Raw keyboard event trace on");}}
    else if(line=="keys off") keyboard.trace=false;
    else if(line=="answer") action(CellActionKind::Answer);
    else if(line=="hangup") action(CellActionKind::Hangup);
    else if(line.startsWith("dial ")) {
        Text number=line.substring(5);
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
    while(platform::console.available()) {
        char c=char(platform::console.read());
        if(c=='\r') continue;
        if(c=='\n') {
            if(serialOverflow) inform("Command too long; discarded"); else command(serialLine);
            serialLine=""; serialOverflow=false;
        } else if(serialLine.length()<512 && !serialOverflow) serialLine+=c;
        else serialOverflow=true;
    }
}
enum class ConversationType {NewSms,Ai,Radio,Sms};
struct ConversationRow {ConversationType type;Text key,title,preview,peer;};
std::vector<ConversationRow> conversationRows() {
    std::vector<deck::ConversationMessage> messages;
    for(const auto& m:store.messages())messages.push_back({m.medium==Medium::Radio,m.sequence,m.peer,m.text});
    const auto indexed=deck::conversationIndex(messages);
    Text aiPreview=ai.busy?"Thinking...":ai.messages().empty()?Text(aiSettings.values().model):Text(ai.messages().back().text.c_str());
    std::vector<ConversationRow> rows={{ConversationType::Ai,"ai","AI Assistant",aiPreview,""},
        {ConversationType::Radio,"lora:broadcast","Meshtastic","Channel conversation",""}};
    for(const auto& row:indexed) {
        if(row.kind==deck::ConversationKind::NearbyLoRa)rows[1].preview=row.preview.c_str();
        else rows.push_back({ConversationType::Sms,row.key.c_str(),row.replyAddress.c_str(),row.preview.c_str(),row.replyAddress.c_str()});
    }
    return rows;
}
void syncConversation(const std::vector<ConversationRow>& rows) {
    for(unsigned n=0;n<rows.size();++n)if(rows[n].key==selectedConversation){conversationCursor=n;return;}
    conversationCursor=0;selectedConversation=rows[0].key;
}
void chooseConversation() {
    const auto rows=conversationRows();syncConversation(rows);const auto& row=rows[conversationCursor];
    if(row.type==ConversationType::NewSms){page=Page::SmsNumber;input="";changed();}
    else if(row.type==ConversationType::Ai)openAi(Page::Messages);
    else if(row.type==ConversationType::Radio)openRadio(Page::Messages);
    else openSms(row.peer);
}
void sendConversation() {
    if(input.isEmpty() || !radiosAllowed())return;
    if(page==Page::Ai) {
        if(!ai.send(aiSettings.values(),input)){changed();return;}
        aiPending=input;aiTransport=ai.status.find("LTE")!=std::string::npos?"LTE":"Wi-Fi";
    } else if(page==Page::SmsThread) {
        deck::SmsSubmit checked;
        if(!deck::encodeSms(recipient.c_str(),input.c_str(),checked)){inform("This message exceeds one GSM-7 SMS");return;}
        smsSend(recipient,input);
    } else radioSend(input);
    input="";threadScroll=0;changed();
}
void key(char c) {
    if(!c) return;
    if(c==27){
        cancelPair();
        if(inConversation()){if(threadReturn==Page::Messages)openMessages();else home();}
        else if(page==Page::SmsNumber){const bool location=pendingLocationText.length();pendingLocationText="";if(location)openCategory(Page::Location);else openMessages();}
        else if(page==Page::AiModelPicker || page==Page::AiEndpoint || page==Page::AiModel || page==Page::AiKey)openAiSetup(aiSetupReturn);
        else if(page==Page::AiSetup){if(aiSetupReturn==Page::Ai)openAi(threadReturn);else if(aiSetupReturn==Page::MessagingSettings)openCategory(Page::MessagingSettings);else openSettings();}
        else if(page==Page::Connections || page==Page::MessagingSettings || page==Page::DisplaySettings || page==Page::Location || page==Page::Diagnostics)openSettings();
        else if(page==Page::Wifi || page==Page::Ble || page==Page::CellularSettings)openCategory(Page::Connections);
        else if(page==Page::MeshSettings){if(meshSetupReturn==Page::Radio)openRadio(threadReturn);else openCategory(Page::MessagingSettings);}
        else if(page==Page::MeshRegion || page==Page::MeshPreset || page==Page::MeshChannel || page==Page::MeshKey || page==Page::MeshNodes)openCategory(Page::MeshSettings);
        else if(page==Page::Apn || page==Page::Carriers)openCategory(Page::CellularSettings);
        else if(page==Page::WifiSsid || page==Page::WifiPassword || page==Page::WifiForget)openWifi();
        else if(page==Page::BleForget)openBle();else home();
        return;
    }
    if(page==Page::Home) {
        if(c=='j' || c==18){homeCursor=(homeCursor+1)%3;changed();return;}
        if(c=='k' || c==17){homeCursor=(homeCursor+2)%3;changed();return;}
        if(c=='\r' || c=='\n')c="mps"[homeCursor];
        if(c=='m'){openMessages();return;}if(c=='s'){openSettings();return;}
        if(c=='p'){page=Page::Phone;input="";changed();return;}
        if(c=='l'){openRadio();return;}if(c=='a'){openAi();return;}
        if(c=='d'){openCategory(Page::Diagnostics);return;}return;
    }
    if(page==Page::Settings || page==Page::Connections || page==Page::MessagingSettings || page==Page::DisplaySettings || page==Page::CellularSettings) {
        const unsigned count=page==Page::Settings?6:page==Page::Connections?3:page==Page::MessagingSettings?2:page==Page::DisplaySettings?2:3;
        const char* shortcuts=page==Page::Settings?"acmdli":page==Page::Connections?"wbc":page==Page::MessagingSettings?"im":page==Page::DisplaySettings?"fk":"acr";
        if(c=='j' || c==18){settingsCursor=(settingsCursor+1)%count;changed();return;}
        if(c=='k' || c==17){settingsCursor=(settingsCursor+count-1)%count;changed();return;}
        if(c=='\r' || c=='\n')c=shortcuts[settingsCursor];
        if(page==Page::Settings){
            if(c=='a'){setAirplaneMode(!airplaneMode());inform(airplaneStatus());}
            else if(c=='c')openCategory(Page::Connections);else if(c=='m')openCategory(Page::MessagingSettings);
            else if(c=='d')openCategory(Page::DisplaySettings);else if(c=='l')openCategory(Page::Location);else if(c=='i')openCategory(Page::Diagnostics);
        } else if(page==Page::Connections){if(c=='w')openWifi();else if(c=='b')openBle();else if(c=='c')openCategory(Page::CellularSettings);}
        else if(page==Page::MessagingSettings){if(c=='i')openAiSetup(Page::MessagingSettings);else if(c=='m'){meshSetupReturn=Page::MessagingSettings;openCategory(Page::MeshSettings);}}
        else if(page==Page::DisplaySettings){const auto& v=settings.values();if(c=='f')saveLights(!v.frontLight,v.keyboardLight);else if(c=='k')saveLights(v.frontLight,!v.keyboardLight);}
        else {if(!radiosAllowed())return;if(c=='a'){input=settings.values().apn;page=Page::Apn;changed();}else if(c=='c')carrierScan();else if(c=='r'){action(CellActionKind::Refresh);inform("Refreshing cellular status");}}
        return;
    }
    if(page==Page::MeshSettings) {
        if(c=='j' || c==18){settingsCursor=(settingsCursor+1)%6;changed();return;}
        if(c=='k' || c==17){settingsCursor=(settingsCursor+5)%6;changed();return;}
        if(c=='\r' || c=='\n')c="mrpcnk"[settingsCursor];
        if(!radiosAllowed())return;
        auto options=settings.mesh();
        if(c=='m'){if(!options.enabled && options.region==deck::MeshRegion::Unset){inform("Choose your region before turning Meshtastic on");return;}options.enabled=!options.enabled;saveMesh(options);}
        else if(c=='r'){openCategory(Page::MeshRegion);settingsCursor=options.region==deck::MeshRegion::US?1:0;}
        else if(c=='p'){openCategory(Page::MeshPreset);settingsCursor=unsigned(options.preset);}
        else if(c=='c'){openCategory(Page::MeshChannel);input=options.channel;}
        else if(c=='n')openCategory(Page::MeshNodes);
        else if(c=='k'){keyboard.trace=false;openCategory(Page::MeshKey);}
        return;
    }
    if(page==Page::MeshRegion || page==Page::MeshPreset) {
        const unsigned count=page==Page::MeshRegion?2:3;
        if(c=='j' || c==18){settingsCursor=(settingsCursor+1)%count;changed();return;}
        if(c=='k' || c==17){settingsCursor=(settingsCursor+count-1)%count;changed();return;}
        if(c=='\r' || c=='\n'){auto options=settings.mesh();if(page==Page::MeshRegion){options.region=settingsCursor?deck::MeshRegion::US:deck::MeshRegion::Unset;if(!settingsCursor)options.enabled=0;}else {const bool defaultName=!std::strcmp(options.channel,deck::meshPresetName(options.preset));options.preset=deck::MeshPreset(settingsCursor);if(defaultName)snprintf(options.channel,sizeof(options.channel),"%s",deck::meshPresetName(options.preset));}saveMesh(options);}
        return;
    }
    if(page==Page::MeshChannel || page==Page::MeshKey) {
        if(c=='\b'){if(input.length())input.remove(input.length()-1);changed(true);}
        else if(c=='\r' || c=='\n') {
            auto options=settings.mesh();
            if(page==Page::MeshChannel){if(input.empty()){inform("Enter a channel name");return;}std::memset(options.channel,0,sizeof(options.channel));std::memcpy(options.channel,input.data(),input.size());}
            else {
                if(input.empty()){openCategory(Page::MeshSettings);return;}
                if(input.size()!=32 && input.size()!=64){inform("Use 32 or 64 hexadecimal characters");return;}
                auto nibble=[](char v)->int{return v>='0' && v<='9'?v-'0':v>='a' && v<='f'?v-'a'+10:v>='A' && v<='F'?v-'A'+10:-1;};
                std::memset(options.psk,0,sizeof(options.psk));options.keyBytes=input.size()/2;
                for(unsigned n=0;n<options.keyBytes;++n){const int hi=nibble(input[n*2]),lo=nibble(input[n*2+1]);if(hi<0 || lo<0){inform("Use hexadecimal digits only");return;}options.psk[n]=uint8_t((hi<<4)|lo);}
            }
            saveMesh(options);
        } else if(c>=32 && c<=126 && input.length()<(page==Page::MeshChannel?11:64)){input+=c;changed(true);}
        return;
    }
    if(page==Page::MeshNodes) {
        if(c=='a'){if(radiosAllowed()){if(!radio.announce())inform(radio.status());else inform("Announcing this device...");}}
        else if(c=='j' || c==18){settingsCursor=std::min(settingsCursor+1,3u);changed();}
        else if(c=='k' || c==17){if(settingsCursor)--settingsCursor;changed();}
        return;
    }
    if(page==Page::Location) {
        if(c=='g') {
            if(!radiosAllowed())return;
            auto options=settings.radios();options.gpsEnabled=!options.gpsEnabled;
            if(options.gpsEnabled && !radiosAllowed())return;
            if(!settings.saveRadios(options)){inform("GPS setting could not be saved");return;}
            gps.configure(options.gpsEnabled);inform(gps.snapshot().status);
        } else if(c=='s')shareLocation();
        return;
    }
    if(page==Page::Messages) {
        if(c=='n'){pendingLocationText="";page=Page::SmsNumber;input="";changed();return;}
        const auto rows=conversationRows();syncConversation(rows);
        if((c=='j' || c==18) && conversationCursor+1<rows.size())++conversationCursor;
        else if((c=='k' || c==17) && conversationCursor)--conversationCursor;
        else if(c=='\r' || c=='\n'){chooseConversation();return;}
        selectedConversation=rows[conversationCursor].key;changed();return;
    }
    if(inConversation()) {
        if(c==17){const unsigned count=threadLineCount();const unsigned max=count>ThreadRows?count-ThreadRows:0;threadScroll=std::min(threadScroll+ThreadRows,max);changed();}
        else if(c==18){threadScroll=threadScroll>ThreadRows?threadScroll-ThreadRows:0;changed();}
        else if(c=='\r' || c=='\n')sendConversation();
        else if(c=='\b'){if(input.length())input.remove(input.length()-1);changed(true);}
        else if(c>=32 && c<=126 && input.length()<(page==Page::Ai?deck::AiPromptLimit:160)){input+=c;threadScroll=0;changed(true);}
        return;
    }
    if(page==Page::AiSetup) {
        const auto& value=aiSettings.values();
        if(c=='e'){input=value.endpoint;page=Page::AiEndpoint;changed();}
        else if(c=='m')openAiModelPicker();
        else if(c=='k'){input="";keyboard.trace=false;page=Page::AiKey;changed();}
        else if(c=='x')configureAi(AiSettings::Field::Key,"","AI key removed");
        else if(c=='c'){if(ai.clear()){threadScroll=0;inform("Conversation cleared");}else inform(ai.status);}
        else if(c=='a')openAi(aiSetupReturn==Page::Ai?threadReturn:Page::Home);
        return;
    }
    if(page==Page::AiModelPicker) {
        if((c=='j' || c==18) && modelCursor<deck::AiModelPresetCount){++modelCursor;changed();}
        else if((c=='k' || c==17) && modelCursor){--modelCursor;changed();}
        else if(c=='\r' || c=='\n') {
            if(modelCursor==deck::AiModelPresetCount){input=aiSettings.values().model;page=Page::AiModel;changed();}
            else configureAi(AiSettings::Field::Model,deck::AiModelPresets[modelCursor].id,"AI model saved");
        }
        return;
    }
    if(page==Page::AiEndpoint || page==Page::AiModel || page==Page::AiKey) {
        const size_t limit=page==Page::AiEndpoint?191:page==Page::AiModel?95:255;
        if(c=='\b'){if(input.length())input.remove(input.length()-1);changed(true);}
        else if(c=='\r' || c=='\n') {
            saveAiField();
        } else if(c>=32 && c<=126 && input.length()<limit){input+=c;changed(true);}
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
            else {input=wifiName==settings.wireless().ssid?Text(settings.wireless().password):Text("");page=Page::WifiPassword;keyboard.trace=false;changed();}
        }
        return;
    }
    if(page==Page::WifiSsid || page==Page::WifiPassword) {
        if(c=='\b'){if(input.length())input.remove(input.length()-1);changed(true);}
        else if(c=='\r' || c=='\n') {
            if(page==Page::WifiSsid) {if(input.isEmpty())return;wifiName=input;input="";wifiNeedsPassword=false;page=Page::WifiPassword;keyboard.trace=false;changed();}
            else connectWifi(input);
        } else if(c>=32 && c<=126 && input.length()<(page==Page::WifiSsid?32:64)){input+=c;changed(true);}
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
            if(c=='\b'){if(input.length())input.remove(input.length()-1);changed(true);}
            else if((c=='\r' || c=='\n') && input.length()==6){bleRequest(BleCommand::Passkey,"",0,input.toInt(),pairHandle);input="";inform("Passkey sent");}
            else {
                const char* digits="wersdfzxcp";const char* mapped=strchr(digits,c);if(mapped)c=mapped-digits==9?'0':char('1'+mapped-digits);
                if(c>='0' && c<='9' && input.length()<6){input+=c;changed(true);}
            }
        }
        return;
    }
    if(page==Page::Apn) {
        if(c=='\b') {if(input.length())input.remove(input.length()-1);changed(true);}
        else if(c=='\r' || c=='\n') saveApn(input);
        else if(c>=32 && c<=126 && input.length()<63) {input+=c;changed(true);}
        return;
    }
    if(page==Page::Diagnostics) {if(c=='r') action(CellActionKind::Refresh); if(c=='i') action(CellActionKind::ReadInbox); return;}
    if(page==Page::Phone) {
        if((c=='a' || c=='\r' || c=='\n') && (callState==4 || callState==5)){action(CellActionKind::Answer);return;}
        if(c=='h' && callState>=0){if(action(CellActionKind::Hangup))phoneFeedback="Ending call...";else phoneFeedback=notice;return;}
    }
    if(c=='\b') {if(input.length())input.remove(input.length()-1);changed(true);return;}
    if(c=='\r' || c=='\n') {
        if(page==Page::SmsNumber) {
            if(deck::phoneNumber(input.c_str())) {
                if(pendingLocationText.length() && uint32_t(millis()-locationCapturedAt)>30000){inform("GPS fix expired. Share again from Location.");return;}
                Text peer=input;openSms(peer);
                if(pendingLocationText.length()){input=pendingLocationText;pendingLocationText="";inform("Review the location link. Send when ready.");}
            }
            else inform("Use digits and optional leading +");
        } else if(page==Page::Phone) {
            if(callState>=0){phoneFeedback="A call is already active";inform(phoneFeedback);}
            else if(deck::phoneNumber(input.c_str())) {
                if(action(CellActionKind::Dial,0,input)){caller=input;input="";phoneFeedback="Calling...";}
                else phoneFeedback=notice;
            } else {phoneFeedback="Enter a phone number with an optional leading +";inform(phoneFeedback);}
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
        if(!((c>='0' && c<='9') || (c=='+' && input.isEmpty()) || (page==Page::Phone && (c=='*' || c=='#')))) return;
        if(input.length()>=16) return;
    } else if(input.length()>=160) return;
    input+=c;changed(true);
}
void text(int x,int y,const Text& value,int scale=1) {
    auto& s=*board::screen;
    s.setTextSize(scale); s.setCursor(x,y); s.print(value);
}
Text clipped(const Text& s,unsigned count) {return s.length()>count?s.substring(0,count-1)+"~":s;}
void wrap(const Text& value,int x,int y,int columns,int rows,int scale=1) {
    Text clean;
    // Built-in font is ASCII; keep received UTF-8 in storage and USB output.
    for(unsigned i=0;i<value.length();++i) {
        unsigned char c=value[i];
        if(c=='\n' || (c>=32 && c<=126)) clean+=char(c);
        else if(c>=0xc0) clean+='?';
    }
    unsigned p=0;
    for(int row=0;row<rows && p<clean.length();++row) {
        unsigned end=std::min(p+unsigned(columns),unsigned(clean.length()));
        int nl=clean.indexOf('\n',p);
        if(nl>=0 && unsigned(nl)<end) end=unsigned(nl);
        text(x,y+row*10*scale,clean.substring(p,end),scale);
        p=end; if(p<clean.length() && clean[p]=='\n')++p;
    }
}
void button(deck::UiRect r,const Text& label,bool filled=false){deck::drawUiButton(*board::screen,r,label,filled);}
void pageTitle(const Text& value){text(10,44,"<",2);text(38,44,value,2);}
void settingRow(unsigned index,const Text& label,const Text& detail,char shortcut,bool disabled=false) {
    deck::drawSettingRow(*board::screen,index,label,disabled?Text("Off in airplane mode"):detail,shortcut,index==settingsCursor);
}
Text cellularSummary(){return airplaneMode()?Text("Airplane mode"):!linked?Text("Cellular unavailable"):!simReady?Text("Check SIM card"):Text(networkName());}
Text pageNotice() {
    if(airplaneMode() && page!=Page::DisplaySettings && page!=Page::AiSetup && page!=Page::AiModelPicker && page!=Page::AiKey && page!=Page::AiModel && page!=Page::AiEndpoint)return airplaneStatus();
    if(page==Page::Ai)return aiScreenStatus();
    if(page==Page::Home)return "";
    if(page==Page::Phone)return phoneFeedback.length()?phoneFeedback:Text("Enter a number, then tap Call.");
    return notice;
}
void phoneView() {
    pageTitle("Phone");text(10,68,clipped(callState>=0?call:cellularSummary(),36));
    button(deck::PhonePlus,"+");button(deck::PhoneErase,"<");
    Text number=callState>=0 && caller.length()?caller:input;
    if(number.empty())text(43,90,"Phone number");
    else {const int scale=number.length()>13?1:2;const Text visible=clipped(number,26);text(40+(160-int(visible.length())*6*scale)/2,scale==1?92:88,visible,scale);}
    deck::drawPhonePad(*board::screen);
    button(deck::PhonePrimary,callState==4 || callState==5?"Answer":callState>=0?"In call":"Call",!airplaneMode());
    button(deck::PhoneEnd,callState>=0?"End":"Clear");
}
using ChatLine=deck::UiChatLine;
using ChatLines=deck::UiChatLines;
void appendBubble(ChatLines& lines,const Text& sender,const Text& body,bool outgoing,unsigned group) {
    deck::appendChatBubble(lines,clipped(sender,30),body,outgoing,group);
}
ChatLines threadLines() {
    ChatLines lines;unsigned group=0;
    if(page==Page::Ai) {
        for(const auto& m:ai.messages())appendBubble(lines,m.assistant?"AI Assistant":"You",m.text.c_str(),!m.assistant,group++);
        if(ai.busy && aiPending.length())appendBubble(lines,"You / sending",aiPending,true,group++);
    } else {
        deck::Conversation conversation;
        conversation.kind=page==Page::Radio?deck::ConversationKind::NearbyLoRa:deck::ConversationKind::Sms;
        conversation.key=page==Page::Radio?"lora:broadcast":deck::conversationPeerKey(recipient.c_str());
        std::vector<const Message*> messages;
        for(const auto& m:store.messages())if(deck::conversationMatches(conversation,{m.medium==Medium::Radio,m.sequence,m.peer,m.text}))messages.push_back(&m);
        std::sort(messages.begin(),messages.end(),[](const Message* a,const Message* b){return a->sequence<b->sequence;});
        for(const auto* m:messages)appendBubble(lines,m->incoming?(page==Page::Radio?Text(m->peer):Text("SMS")):Text("You / ")+deliveryName(m->delivery,m->medium),m->text,!m->incoming,group++);
    }
    while(!lines.empty() && lines.back().gap)lines.pop_back();
    return lines;
}
unsigned threadLineCount(){return threadLines().size();}
void conversationsView() {
    auto& s=*board::screen;const auto rows=conversationRows();syncConversation(rows);
    pageTitle("Messages");button({190,40,40,23},"New");text(10,69,"SMS, AI and Meshtastic");
    const unsigned offset=(conversationCursor/4)*4;
    for(unsigned n=0;n<4 && offset+n<rows.size();++n) {
        const auto& row=rows[offset+n];const int y=84+int(n)*41;
        if(offset+n==conversationCursor)s.drawRoundRect(10,y,220,39,5,deck::Ink);
        text(19,y+6,clipped(row.title,33));wrap(row.preview,19,y+22,33,1);
    }
    s.drawRoundRect(10,250,106,18,3,deck::Ink);text(38,255,"Previous");
    s.drawRoundRect(124,250,106,18,3,deck::Ink);text(165,255,"Next");
}
void composerView() {
    auto& s=*board::screen;
    s.drawRoundRect(10,214,220,54,5,deck::Ink);s.drawRoundRect(193,219,31,43,4,deck::Ink);text(197,237,"Send");
    Text draft=input.isEmpty()?Text("Message..."):input+"_";
    if(draft.length()>84)draft="<"+draft.substring(draft.length()-83);
    wrap(draft,18,225,28,3);
}
void threadView() {
    auto& s=*board::screen;
    text(10,44,"<",2);text(38,44,page==Page::Ai?"AI Assistant":page==Page::Radio?"Meshtastic":clipped(recipient,16),2);
    if(page==Page::Ai || page==Page::Radio){s.drawRoundRect(203,40,27,23,4,deck::Ink);text(208,48,"...");}
    text(10,69,clipped(page==Page::Ai?aiConnectionText():page==Page::Radio?Text("Channel group"):Text("SMS / ")+networkName(),36));
    const auto lines=threadLines();const unsigned maxScroll=lines.size()>ThreadRows?lines.size()-ThreadRows:0;
    threadScroll=std::min(threadScroll,maxScroll);
    const unsigned end=lines.size()-threadScroll,start=end>ThreadRows?end-ThreadRows:0;
    if(lines.empty()) {
        if(page==Page::Ai){wrap(ai.busy?Text("Waiting for your provider..."):aiStartHint(),18,103,33,3);text(18,150,clipped(aiSettings.values().model,33));}
        else if(page==Page::Radio)wrap("Messages are shared with devices on your Meshtastic channel.",18,102,33,6);
        else wrap("Start your conversation below.",18,112,33,3);
    }
    for(unsigned n=start;n<end;) {
        if(lines[n].gap){++n;continue;}
        unsigned next=n+1;while(next<end && lines[next].group==lines[n].group && !lines[next].gap)++next;
        const int x=lines[n].outgoing?30:10,y=80+int(n-start)*10,h=int(next-n)*10+4;
        if(lines[n].outgoing)s.fillRoundRect(x,y,200,h,4,deck::Ink);else s.drawRoundRect(x,y,200,h,4,deck::Ink);
        s.setTextColor(lines[n].outgoing?deck::Paper:deck::Ink);
        for(unsigned row=n;row<next;++row)text(x+7,83+int(row-start)*10,lines[row].value);
        s.setTextColor(deck::Ink);n=next;
    }
    s.drawRoundRect(10,190,75,17,3,deck::Ink);text(28,195,"Older");
    s.drawRoundRect(95,190,75,17,3,deck::Ink);text(113,195,"Newer");
    composerView();
}

void captureBusyKeys(const void*) {
    const uint32_t now=millis();
    if(uint32_t(now-busyPollAt)>=2) {
        busyPollAt=now;
        // A queued navigation key may open secret entry only after this frame.
        // Suppress raw event tracing for the entire captured batch.
        const bool trace=keyboard.trace;keyboard.trace=false;
        for(unsigned n=0;n<16;++n){const char c=keyboard.poll();if(!c)break;if(busyKeys.push(c,millis()))++frameQueuedKeys;}
        keyboard.trace=trace;
    }
    // This callback never renders, changes pages or touches shared SPI/radio.
    delay(1);
}
void applyBusyKeys() {
    if(busyKeys.overflowed() || keyboard.lostEvents) {
        keyboard.lostEvents=false;
        busyKeys.reset();home();inform("Keyboard buffer filled; draft canceled. Please retype.");return;
    }
    char c=0;
    while(busyKeys.pop(c,busyKeyTime)) {
        // Only typing uses the capture time. Navigation and Enter still run
        // here, after the old image completes, with an immediate redraw.
        applyingBusyKey=true;key(c);applyingBusyKey=false;
    }
}
void touchAt(int x,int y) {
    // Never interpret a tap against a page or draft newer than the visible
    // e-paper image. Send, Call and Share all have separate explicit targets.
    if(ui.pending || !board::screen)return;
    if(y>=303){if(x>=170)home();else key(27);return;}
    if(page!=Page::Home && x<36 && y>=36 && y<=65){key(27);return;}
    if(page==Page::Home){for(unsigned n=0;n<3;++n)if(deck::homeRow(n).contains(x,y)){homeCursor=n;key("mps"[n]);return;}}
    else if(page==Page::Phone) {
        if(const char digit=deck::phoneKeyAt(x,y)){key(digit);return;}
        if(deck::PhonePlus.contains(x,y))key('+');
        else if(deck::PhoneErase.contains(x,y))key('\b');
        else if(deck::PhonePrimary.contains(x,y)){if(callState==4 || callState==5)key('a');else if(callState<0)key('\r');}
        else if(deck::PhoneEnd.contains(x,y)){if(callState>=0)key('h');else{input="";changed();}}
    } else if(page==Page::Messages) {
        if(deck::UiRect{190,40,40,23}.contains(x,y)){key('n');return;}
        const auto rows=conversationRows();syncConversation(rows);
        if(x>=10 && x<=230 && y>=250 && y<=268){conversationCursor=x<120?(conversationCursor>=4?conversationCursor-4:0):std::min(conversationCursor+4,unsigned(rows.size()-1));selectedConversation=rows[conversationCursor].key;changed();}
        else if(x>=10 && x<=230 && y>=84 && y<248){const unsigned row=(y-84)/41,selected=(conversationCursor/4)*4+row;if((y-84)%41<39 && selected<rows.size()){conversationCursor=selected;selectedConversation=rows[selected].key;chooseConversation();}}
    } else if(inConversation()) {
        if(y>=190 && y<=207){if(x>=10 && x<=85)key(17);else if(x>=95 && x<=170)key(18);}
        else if(deck::UiRect{193,219,31,43}.contains(x,y) && input.length())key('\r');
        else if(deck::UiRect{203,40,27,23}.contains(x,y)){if(page==Page::Ai)openAiSetup(Page::Ai);else if(page==Page::Radio){meshSetupReturn=Page::Radio;openCategory(Page::MeshSettings);}}
    } else if(page==Page::SmsNumber){if(deck::UiRect{10,223,220,35}.contains(x,y))key('\r');}
    else if(page==Page::Settings || page==Page::Connections || page==Page::MessagingSettings || page==Page::DisplaySettings || page==Page::CellularSettings){
        const unsigned count=page==Page::Settings?6:page==Page::Connections?3:page==Page::MessagingSettings?2:page==Page::DisplaySettings?2:3;
        for(unsigned n=0;n<count;++n)if(deck::settingsRow(n).contains(x,y)){settingsCursor=n;key('\r');return;}
    } else if(page==Page::MeshSettings || page==Page::MeshRegion || page==Page::MeshPreset) {
        const unsigned count=page==Page::MeshSettings?6:page==Page::MeshRegion?2:3;
        for(unsigned n=0;n<count;++n)if(deck::settingsRow(n).contains(x,y)){settingsCursor=n;key('\r');return;}
    } else if(page==Page::MeshChannel || page==Page::MeshKey) {
        if(deck::UiRect{10,211,220,26}.contains(x,y))key('\r');
        else if(page==Page::MeshKey && deck::UiRect{10,242,220,26}.contains(x,y))usePublicMeshKey();
    } else if(page==Page::MeshNodes) {
        if(deck::UiRect{10,244,106,25}.contains(x,y))key('a');
        else if(deck::UiRect{124,244,48,25}.contains(x,y))key('k');else if(deck::UiRect{182,244,48,25}.contains(x,y))key('j');
    } else if(page==Page::Location){if(deck::settingsRow(0).contains(x,y))key('g');else if(deck::UiRect{10,221,220,36}.contains(x,y))key('s');}
    else if(page==Page::Wifi) {
        if(deck::UiRect{174,40,56,24}.contains(x,y))key('w');
        else if(deck::UiRect{10,101,106,22}.contains(x,y))key('r');else if(deck::UiRect{124,101,106,22}.contains(x,y))key('n');
        else if(deck::UiRect{10,248,220,21}.contains(x,y))key('f');
        else if(x>=10 && x<=230 && y>=128 && y<224){const unsigned selected=(wifiCursor/4)*4+(y-128)/24;if(selected<wifi.networks.size()){wifiCursor=selected;key('\r');}}
    } else if(page==Page::Ble) {
        if(deck::UiRect{186,40,44,24}.contains(x,y))key('b');
        else if(deck::UiRect{10,115,66,22}.contains(x,y))key('r');else if(deck::UiRect{84,115,66,22}.contains(x,y))key('p');else if(deck::UiRect{158,115,72,22}.contains(x,y))key('c');
        else if(deck::UiRect{10,248,150,21}.contains(x,y))key('f');else if(deck::UiRect{170,248,60,21}.contains(x,y))key('x');
        else if(x>=10 && x<=230 && y>=151 && y<226){const unsigned selected=(bleCursor/3)*3+(y-151)/25;if(selected<bleDevices.size()){bleCursor=selected;key('\r');}}
    } else if(page==Page::AiSetup){const char* actions="mkecxa";for(unsigned n=0;n<6;++n)if(deck::settingsRow(n).contains(x,y)){key(actions[n]);return;}}
    else if(page==Page::AiModelPicker && x>=10 && x<=230 && y>=77 && y<=255){const unsigned row=(y-77)/36;if(row<=deck::AiModelPresetCount && (y-77)%36<34){modelCursor=row;key('\r');}}
    else if((page==Page::AiEndpoint || page==Page::AiModel || page==Page::AiKey || page==Page::WifiSsid || page==Page::WifiPassword) && deck::UiRect{10,211,220,26}.contains(x,y))key('\r');
    else if(page==Page::Apn && deck::UiRect{10,165,220,32}.contains(x,y))key('\r');
    else if(page==Page::WifiForget || page==Page::BleForget){if(deck::UiRect{10,176,106,30}.contains(x,y))key('y');else if(deck::UiRect{124,176,106,30}.contains(x,y))key('n');}
    else if(page==Page::BlePair && pairMode==BleEventKind::PairConfirm){if(deck::UiRect{10,169,106,30}.contains(x,y))key('y');else if(deck::UiRect{124,169,106,30}.contains(x,y))key('n');}
}

void draw() {
    if(!board::screen || !ui.pending) return;
    uint32_t now=millis();
    if(!ui.ready(now))return;
    const uint32_t snapshot=ui.version,started=millis();
    const bool composerOnly=refreshes && refreshes%16!=0 && !ui.urgent && inConversation() && page==renderedPage && threadScroll==renderedScroll;
    frameQueuedKeys=0;
    auto& s=*board::screen;
    if(refreshes%16==0)s.setFullWindow();
    else if(composerOnly)s.setPartialWindow(0,214,240,54);
    else s.setPartialWindow(0,0,240,320);
    if(composerOnly){s.fillRect(0,214,240,54,deck::Paper);composerView();}
    else {
        s.fillScreen(deck::Paper);
        text(10,13,airplaneMode()?"AIRPLANE":"HANDHELD");
        const auto wireless=deck::wifiIndicator(wifi.enabled && !airplaneMode(),wifi.connected && !airplaneMode(),wifi.connecting && !airplaneMode(),wifi.rssi);
        text(83,13,wireless.label);
        for(unsigned bar=0;bar<3;++bar) {
            const int h=3+int(bar)*3,x=112+int(bar)*4;
            if(bar<wireless.bars)s.fillRect(x,23-h,3,h,deck::Ink);
            else s.drawRect(x,23-h,3,h,deck::Ink);
        }
        const auto cell=airplaneMode()?deck::CellularIndicator{"Off",0}:deck::cellularIndicator(linked,simReady,network,signalDbm,operatorName.c_str(),systemInfo.c_str());
        text(133,13,cell.label);
        for(unsigned bar=0;bar<4;++bar) {
            int h=3+int(bar)*3,x=157+int(bar)*4;
            if(bar<cell.bars)s.fillRect(x,23-h,3,h,deck::Ink);
            else s.drawRect(x,23-h,3,h,deck::Ink);
        }
        Text batteryText=battery>=0?Text(battery)+"%":"--%";
        text(230-int(batteryText.length())*6,13,batteryText);
        s.drawFastHLine(10,30,220,deck::Ink);
        if(page==Page::Home) {
            text(10,44,"Handheld",2);
            const char* labels[]={"Messages","Phone","Settings"};
            const Text details[]={Text("SMS, AI and Meshtastic"),cellularSummary(),Text("Connections and device")};
            for(unsigned n=0;n<3;++n)deck::drawHomeRow(s,n,labels[n],details[n],"MPS"[n],n==homeCursor);
        } else if(page==Page::Messages) conversationsView();
        else if(inConversation())threadView();
        else if(page==Page::SmsNumber) {
            pageTitle("New message");text(10,78,"To");wrap(input+"_",10,101,18,2,2);
            text(10,163,pendingLocationText.empty()?"Phone number, including country code":"Share your GPS location by SMS");
            text(10,187,"Use the keyboard's printed digits.");
            button({10,223,220,35},"Continue");
        } else if(page==Page::Phone)phoneView();
        else if(page==Page::Settings) {
            pageTitle("Settings");
            settingRow(0,"Airplane mode",airplaneStatus(),'A');
            settingRow(1,"Connections","Wi-Fi, Bluetooth, cellular",'C');
            settingRow(2,"Messaging","AI and Meshtastic",'M');
            settingRow(3,"Display","Screen and keyboard lights",'D');
            settingRow(4,"Location",settings.radios().gpsEnabled?"GPS on":"GPS off",'L');
            settingRow(5,"About","Device and network status",'I');
        } else if(page==Page::Connections) {
            pageTitle("Connections");
            settingRow(0,"Wi-Fi",wifi.status,'W',airplaneMode());
            settingRow(1,"Bluetooth",bleStatus,'B',airplaneMode());
            settingRow(2,"Cellular",cellularSummary(),'C',airplaneMode());
        } else if(page==Page::MessagingSettings) {
            pageTitle("Messaging");settingRow(0,"AI",aiSettings.values().model,'I');
            settingRow(1,"Meshtastic",radio.ready?"Ready":"Set up your channel",'M');
        } else if(page==Page::DisplaySettings) {
            pageTitle("Display");const auto& value=settings.values();
            settingRow(0,"Screen light",board::revision11?(value.frontLight?"On":"Off"):"Not fitted",'F');
            settingRow(1,"Keyboard light",value.keyboardLight?"On":"Off",'K');
        } else if(page==Page::CellularSettings) {
            pageTitle("Cellular");
            settingRow(0,"Access point",settings.values().apn[0]?settings.values().apn:"Modem default",'A',airplaneMode());
            settingRow(1,"Carriers","Search nearby networks",'C',airplaneMode());
            settingRow(2,"Refresh status",cellularSummary(),'R',airplaneMode());
        } else if(page==Page::Location) {
            pageTitle("Location");const auto point=gps.snapshot();
            settingRow(0,"GPS",point.preferredEnabled?"On":"Off",'G',airplaneMode());
            text(17,121,clipped(point.status,34));
            if(point.fix){char value[48];snprintf(value,sizeof(value),"%.6f, %.6f",point.latitude,point.longitude);text(17,146,value);text(17,166,Text(point.satellites)+" satellites");}
            else wrap(point.enabled?"Go outdoors for a clear view of the sky.":"Turn GPS on to find your location.",17,149,33,3);
            button({10,221,220,36},"Share by SMS",point.fix && point.fixAgeMs<=30000 && !airplaneMode());
        } else if(page==Page::MeshRegion || page==Page::MeshPreset) {
            pageTitle(page==Page::MeshRegion?"Region":"Preset");
            if(page==Page::MeshRegion){settingRow(0,"Not selected","Meshtastic stays off",' ');settingRow(1,"United States","US / 902-928 MHz",' ');}
            else {settingRow(0,"Long / Fast","Longer range",' ');settingRow(1,"Medium / Fast","Balanced",' ');settingRow(2,"Short / Fast","Shorter range",' ');}
        } else if(page==Page::MeshChannel || page==Page::MeshKey) {
            const bool secret=page==Page::MeshKey;pageTitle(secret?"Channel key":"Channel");
            Text visible=input;if(secret){visible="";for(unsigned n=0;n<input.size();++n)visible+='*';}
            wrap(visible+"_",10,88,35,9);button({10,211,220,26},"Save");
            if(secret)button({10,242,220,26},"Use public default key");else text(10,250,"Use the same name on each device");
        } else if(page==Page::MeshNodes) {
            pageTitle("Heard nodes");unsigned position=0,shown=0;
            for(const auto& node:radio.nodes())if(node.id){if(position++<settingsCursor*4)continue;if(shown>=4)break;char id[20];snprintf(id,sizeof(id),"!%08lx",(unsigned long)node.id);const int y=80+int(shown++)*39;text(10,y,node.user.longName[0]?node.user.longName:id);text(10,y+15,Text(id)+" / "+Text(node.rssi)+" dBm");}
            if(!shown)wrap("No devices heard yet. Match region, preset, channel and key on your devices.",10,94,35,6);
            button({10,244,106,25},"Announce");button({124,244,48,25},"<");button({182,244,48,25},">");
        } else if(page==Page::MeshSettings) {
            pageTitle("Meshtastic");const auto& mesh=settings.mesh();
            settingRow(0,"Meshtastic",mesh.enabled?"On":"Off",'M',airplaneMode());
            settingRow(1,"Region",mesh.region==deck::MeshRegion::US?"US / 915 MHz":"Choose your region",'R',airplaneMode());
            settingRow(2,"Preset",deck::meshPresetName(mesh.preset),'P',airplaneMode());
            settingRow(3,"Channel",mesh.channel,'C',airplaneMode());
            settingRow(4,"Heard nodes","Nearby devices",'N',airplaneMode());
            settingRow(5,"Channel key",deck::meshPublicKey(mesh)?"Public default - no privacy":mesh.keyBytes==16?"Custom AES-128 key":"Custom AES-256 key",'K',airplaneMode());
        } else if(page==Page::Diagnostics) {
            pageTitle("About");text(10,82,"Handheld OS / native ESP-IDF");
            text(10,108,Text("T-Deck Pro v1.")+(board::revision11?"1":"0"));
            text(10,137,cellularSummary());text(10,157,"Wi-Fi: "+Text(wifi.status));
            text(10,177,radio.ready?"Meshtastic ready":"Meshtastic off / unavailable");
            text(10,197,store.persistent?"Messages saved on device":"Message storage unavailable");
            text(10,237,"More details: USB status command");
        } else if(page==Page::Apn) {
            pageTitle("Access point");
            wrap(input+"_",10,84,35,3);
            button({10,165,220,32},"Save");

            wrap("Use the APN supplied by your carrier.",10,222,35,2);

        } else if(page==Page::Carriers) {
            text(10,44,"CARRIER SCAN",2);wrap(carrierStatus,10,73,35,2);
            for(unsigned n=0;n<4 && carrierOffset+n<carriers.size();++n) {
                const auto& row=carriers[carrierOffset+n];int y=105+n*31;
                text(10,y,clipped(row.code=="313100"?Text("FirstNet"):Text(row.name.c_str()),34));
                const char* state=row.status==1?"Available":row.status==2?"Current":row.status==3?"Forbidden":"Unknown";
                text(10,y+12,Text(row.code.c_str())+"  "+(row.technology==7?Text("LTE"):Text("RAT ")+row.technology)+"  "+state);
            }
            text(10,239,"R Scan again   J/K Page");text(10,255,"Scan does not switch carriers");
        } else if(page==Page::Wifi) {
            pageTitle("Wi-Fi");button({174,40,56,24},wifi.enabled?"On":"Off",wifi.enabled && !airplaneMode());text(10,73,clipped(wifi.status,35));text(10,87,wifi.address.length()?"IP "+wifi.address:Text("2.4 GHz networks"));
            button({10,101,106,22},"Scan");button({124,101,106,22},"Add network");
            unsigned offset=(wifiCursor/4)*4;
            if(wifi.networks.empty())text(10,140,wifi.enabled?"No scan results yet. R scans.":"Wi-Fi is off. W turns it on.");
            for(unsigned n=0;n<4 && offset+n<wifi.networks.size();++n) {
                const auto& row=wifi.networks[offset+n];int y=128+n*24;
                text(10,y,Text(offset+n==wifiCursor?"> ":"  ")+clipped(row.ssid,31));
                text(22,y+11,Text(row.rssi)+" dBm / "+(row.auth==WIFI_AUTH_OPEN?"Open":WifiService::supported(row.auth)?"Password":"Unsupported security"));
            }
            button({10,248,220,21},"Forget saved network");
        } else if(page==Page::WifiSsid || page==Page::WifiPassword) {
            bool secret=page==Page::WifiPassword;
            pageTitle(secret?"Password":"Network name");
            if(secret)wrap(wifiName,10,78,35,2);
            Text visible=input;
            if(secret){visible="";for(unsigned i=0;i<input.length();++i)visible+='*';}
            wrap(visible+"_",10,110,35,3);
            button({10,211,220,26},secret?"Connect":"Continue");
            if(secret)text(10,249,"Password stays on this device");
        } else if(page==Page::WifiForget || page==Page::BleForget) {
            pageTitle("Forget device");
            wrap(page==Page::WifiForget?Text("Remove saved Wi-Fi name and password?"):Text(forgetAllBonds?"Remove all BLE bonds and the saved keyboard?":"Remove the saved BLE keyboard and its bond?"),10,90,35,5);
            button({10,176,106,30},"Forget");button({124,176,106,30},"Cancel");
        } else if(page==Page::Ble) {
            pageTitle("Bluetooth");button({186,40,44,24},bleActive?"On":"Off",bleActive && !airplaneMode());text(10,73,clipped(bleStatus,35));text(10,87,clipped(phoneStatus,35));text(10,101,clipped(keyboardStatus,35));
            button({10,115,66,22},"Scan");button({84,115,66,22},"Phone");button({158,115,72,22},"Reconnect");
            unsigned offset=(bleCursor/3)*3;
            if(bleDevices.empty())text(10,162,bleScanning?"Scanning BLE devices...":"Put keyboard in pairing mode");
            for(unsigned n=0;n<3 && offset+n<bleDevices.size();++n) {
                const auto& row=bleDevices[offset+n];int y=151+n*25;
                text(10,y,Text(offset+n==bleCursor?"> ":"  ")+clipped(row.name,31));
                text(22,y+11,Text(row.rssi)+" dBm "+(row.hid?"HID / keyboard candidate":"BLE device"));
            }
            button({10,248,150,21},"Forget keyboard");button({170,248,60,21},"All bonds");
        } else if(page==Page::BlePair) {
            text(10,44,"BLUETOOTH PAIR",2);
            if(pairMode==BleEventKind::PairConfirm) {
                text(10,83,"Compare this code on both devices");
                char pin[8];snprintf(pin,sizeof(pin),"%06lu",(unsigned long)pairPin);text(10,117,pin,3);
                button({10,169,106,30},"Match");button({124,169,106,30},"Reject");
            } else if(pairMode==BleEventKind::PairDisplay) {
                text(10,83,"Type this code on the other device");
                char pin[8];snprintf(pin,sizeof(pin),"%06lu",(unsigned long)pairPin);text(10,117,pin,3);
                text(10,169,"Then press Enter on that device");
            } else {text(10,83,"Enter the other device's 6-digit code");text(10,117,input+"_",3);text(10,169,"Enter: submit passkey");text(10,190,"W/E/R 1/2/3 | S/D/F 4/5/6");text(10,205,"Z/X/C 7/8/9 | P 0");}
            text(10,241,"Sym cancels. Expires in 60 seconds.");
        } else if(page==Page::AiSetup) {
            const auto& value=aiSettings.values();pageTitle("AI settings");
            settingRow(0,"Model",value.model,'M');settingRow(1,"API key",value.key[0]?"Saved (hidden)":"Not set",'K');
            settingRow(2,"Endpoint",value.endpoint,'E');settingRow(3,"Clear chat","Remove this conversation",'C');
            settingRow(4,"Forget key","Remove saved API key",'X');settingRow(5,"Open chat","Start a conversation",'A');
        } else if(page==Page::AiModelPicker) {
            pageTitle("Choose model");
            for(unsigned n=0;n<=deck::AiModelPresetCount;++n) {
                const int y=77+int(n)*36;
                if(n==modelCursor)s.drawRoundRect(10,y,220,34,4,deck::Ink);
                text(19,y+5,n<deck::AiModelPresetCount?deck::AiModelPresets[n].label:"Custom model");
                text(19,y+19,n<deck::AiModelPresetCount?deck::AiModelPresets[n].id:"Enter a provider model ID");
            }
        } else if(page==Page::AiEndpoint || page==Page::AiModel || page==Page::AiKey) {
            const bool secret=page==Page::AiKey;pageTitle(secret?"API key":page==Page::AiModel?"Model":"Endpoint");
            Text visible=input;if(secret){visible="";for(unsigned n=0;n<input.length();++n)visible+='*';}
            wrap(visible+"_",10,88,35,11);button({10,211,220,26},"Save");
            if(secret)text(10,250,"Blank removes the saved key");
            else if(page==Page::AiEndpoint)text(10,250,"HTTPS API base, e.g. host/v1");
            else text(10,242,"Use your provider's exact model ID");
        }
        s.drawFastHLine(10,273,220,deck::Ink);
        wrap(pageNotice(),10,281,36,2);
        text(10,308,page==Page::Home?"Tap an app":"< Back");
        if(page!=Page::Home)text(200,308,"Home");
    }
    const bool displayed=s.refresh();
    if(!displayed){
        platform::console.println("Display refresh failed");
        lastDrawMs=uint32_t(millis()-started);maxDrawMs=std::max(maxDrawMs,lastDrawMs);
        applyBusyKeys();return;
    }
    // Consume a press that arrived while the blocking e-paper refresh was in
    // flight. It began against the old image and must not activate a new Send.
    int ignoredX=0,ignoredY=0;touch.poll(ignoredX,ignoredY);
    lastDrawMs=uint32_t(millis()-started);maxDrawMs=std::max(maxDrawMs,lastDrawMs);
    renderedPage=page;renderedScroll=threadScroll;++refreshes;
    ui.rendered(snapshot);
    applyBusyKeys();
}
}
void setupDevice() {
    platform::begin(); // Boot on battery without waiting for a USB host.
    const esp_err_t nvsStatus=nvs_flash_init();
    if(nvsStatus!=ESP_OK)platform::console.println("Saved storage unavailable; keeping existing NVS unchanged.");
    board::begin();
    if(board::unsupported) {platform::console.println("T-Deck Pro MAX detected; unsupported profile. Radios disabled.");return;}
    keyboard.begin();
    if(board::screen)board::screen->setBusyCallback(captureBusyKeys);
    touch.begin();
    store.begin();
    settings.begin();
    aiSettings.begin();ai.begin();
    board::lights(settings.values().frontLight,settings.values().keyboardLight);
    battery=board::batteryPercent();
    wifi.changed=[](const std::string& value){inform(Text("Wi-Fi: ")+Text(value));};
    wifi.signalChanged=[](){changed();};
    initializeAirplaneMode(settings,wifi,radio,gps);
    wifi.configure(settings.wireless());
    gps.configure(settings.radios().gpsEnabled);
    bleTask=startBleTask();
    if(bleTask && settings.wireless().bleEnabled) {
        bleRequest(BleCommand::Enable);
        if(!airplaneMode() && settings.wireless().keyboardAddress[0])bleRequest(BleCommand::Connect,settings.wireless().keyboardAddress,settings.wireless().keyboardAddressType);
    }
    radio.changed=[](){if(page==Page::Radio || page==Page::MeshSettings || page==Page::MeshNodes)changed();};
    radio.received=[](const deck::RadioPacket& p,int rssi){
        char peer[20]; snprintf(peer,sizeof(peer),"Node %08lX",(unsigned long)p.sender);
        std::string identity=std::to_string(p.sender)+":"+std::to_string(p.sequence);
        if(store.add(Medium::Radio,true,peer,p.text.c_str(),deck::fingerprint(identity))) inform(Text(peer)+" / "+Text(rssi)+" dBm");
    };
    radio.sent=[](uint32_t id,bool ok){
        if(id)store.update(id,ok?Delivery::Submitted:Delivery::Unknown);
        inform(id?(ok?"Message transmitted; receipt unconfirmed":"Transmission result unknown"):(ok?"Node info announced":"Node announcement failed"));
    };
    radio.configure(settings.mesh());
    cellTask=startCellTask(settings.values().apn);
    if(!cellTask) inform("Could not start cellular task");
    if(!keyboard.ready) inform("Keyboard unavailable; use USB commands");
    help();
    draw();
}
void loopDevice() {
    if(board::unsupported) {delay(100);return;}
    serialPoll(); cellPoll(); wifi.poll(); blePoll();gps.poll();
    if(ai.poll()){aiPending="";threadScroll=0;changed();}
    if(page==Page::Location) {
        const auto point=gps.snapshot();
        if(point.fix!=gpsUiFix || point.enabled!=gpsUiEnabled){gpsUiFix=point.fix;gpsUiEnabled=point.enabled;changed();}
        if(uint32_t(millis()-gpsUiAt)>=2000) {
            gpsUiAt=millis();char signature[128];
            snprintf(signature,sizeof(signature),"%s/%u/%.5f/%.5f/%d",point.status,unsigned(point.satellites),point.latitude,point.longitude,point.fixAgeMs<=30000);
            if(gpsUiSignature!=signature){gpsUiSignature=signature;changed();}
        }
    }
    bool radioWasReady=radio.ready;
    radio.poll();
    if(radio.ready!=radioWasReady) inform(radio.ready?"LoRa receiver restored":"LoRa receive unavailable; retrying");
    for(unsigned i=0;i<16;++i){char c=keyboard.poll();if(!c)break;key(c);}
    if(keyboard.lostEvents){keyboard.lostEvents=false;busyKeys.reset();home();inform("Keyboard events lost; draft canceled. Please retype.");}
    int touchX=0,touchY=0;if(touch.poll(touchX,touchY))touchAt(touchX,touchY);
    if(uint32_t(millis()-batteryAt)>=30000){batteryAt=millis();int value=board::batteryPercent();if(value!=battery){battery=value;changed();}}
    draw();
    delay(5);
}

extern "C" void app_main() {
    setupDevice();
    for(;;)loopDevice();
}
