#include "ble_service.h"
#include <NimBLEDevice.h>
#include <connectivity.h>
#include <algorithm>
#include <cstring>
QueueHandle_t bleRequests=nullptr,bleEvents=nullptr;
std::atomic<uint32_t> droppedBleEvents{0};
namespace {
constexpr const char* UartService="6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* UartRx="6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* UartTx="6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
NimBLEServer* server=nullptr;
NimBLEClient* keyboard=nullptr;
NimBLECharacteristic *statusCharacteristic=nullptr,*batteryCharacteristic=nullptr;
std::atomic<uint16_t> phoneHandle{BLE_HS_CONN_HANDLE_NONE};
std::atomic<bool> statusRequested{false};
std::atomic<uint32_t> pairingUntil{0};
std::atomic<uint32_t> phoneConnectedAt{0};
bool post(BleEvent event) {
    if(xQueueSend(bleEvents,&event,0)==pdTRUE)return true;
    droppedBleEvents.fetch_add(1,std::memory_order_relaxed);return false;
}
void event(BleEventKind kind,const std::string& text="",uint32_t value=0,uint16_t handle=0) {
    BleEvent e{};e.kind=kind;e.value=value;e.handle=handle;snprintf(e.text,sizeof(e.text),"%s",text.c_str());post(e);
}
bool windowOpen(){return pairingUntil.load() && int32_t(pairingUntil.load()-millis())>0;}
void pair(NimBLEConnInfo& info,BleEventKind kind,uint32_t pin=0) {
    BleEvent e{};e.kind=kind;e.value=pin;e.handle=info.getConnHandle();
    snprintf(e.address,sizeof(e.address),"%s",info.getAddress().toString().c_str());
    if(!post(e)) {
        if(kind==BleEventKind::PairConfirm)NimBLEDevice::injectConfirmPasskey(info,false);
        if(info.isMaster()){auto* c=NimBLEDevice::getClientByHandle(info.getConnHandle());if(c)c->disconnect();}
        else if(server)server->disconnect(info);
    }
}
void authenticated(NimBLEConnInfo& info) {
    bool secure=info.isEncrypted() && info.isAuthenticated();
    event(BleEventKind::PairDone,secure?"BLE pairing secured":"BLE pairing failed",secure,info.getConnHandle());
    if(!secure) {
        if(info.isMaster()){auto* c=NimBLEDevice::getClientByHandle(info.getConnHandle());if(c)c->disconnect();}
        else if(server)server->disconnect(info);
    } else if(info.isSlave())event(BleEventKind::Phone,"Phone paired and encrypted",1);
}
class ServerCallbacks:public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s,NimBLEConnInfo& info) override {
        if(s->getConnectedCount()>1 || (!windowOpen() && !NimBLEDevice::isBonded(info.getIdAddress()))) {s->disconnect(info);return;}
        phoneConnectedAt=millis();phoneHandle=info.getConnHandle();pairingUntil=0;
        event(BleEventKind::Phone,"Phone connected; awaiting security",0);
        if(!NimBLEDevice::startSecurity(info.getConnHandle()))s->disconnect(info);
    }
    void onDisconnect(NimBLEServer*,NimBLEConnInfo& info,int) override {
        if(phoneHandle==info.getConnHandle()){
            phoneHandle=BLE_HS_CONN_HANDLE_NONE;event(BleEventKind::Phone,"Phone disconnected",0);
            event(BleEventKind::PairDone,"Phone disconnected",0,info.getConnHandle());
        }
    }
    void onConfirmPassKey(NimBLEConnInfo& info,uint32_t pin) override {pair(info,BleEventKind::PairConfirm,pin);}
    void onPassKeyEntry(NimBLEConnInfo& info) override {pair(info,BleEventKind::PairEntry);}
    uint32_t onPassKeyDisplay() override {
        uint32_t pin=100000+esp_random()%900000;
        event(BleEventKind::PairDisplay,"Enter code on phone",pin,phoneHandle);return pin;
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {authenticated(info);}
} serverCallbacks;
class ClientCallbacks:public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*,int) override {event(BleEventKind::Keyboard,"Keyboard disconnected",0);}
    void onConfirmPasskey(NimBLEConnInfo& info,uint32_t pin) override {pair(info,BleEventKind::PairConfirm,pin);}
    void onPassKeyEntry(NimBLEConnInfo& info) override {pair(info,BleEventKind::PairEntry);}
    uint32_t onPassKeyDisplay(NimBLEConnInfo& info) override {
        uint32_t pin=100000+esp_random()%900000;pair(info,BleEventKind::PairDisplay,pin);return pin;
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {authenticated(info);}
} clientCallbacks;
class ScanCallbacks:public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* d) override {
        BleEvent e{};e.kind=BleEventKind::Device;e.value=d->isAdvertisingService(NimBLEUUID(uint16_t(0x1812))) || d->getAppearance()==0x03c1;
        if(!e.value && d->getName().empty())return;
        e.handle=uint16_t(int16_t(d->getRSSI()));e.addressType=d->getAddressType();
        snprintf(e.address,sizeof(e.address),"%s",d->getAddress().toString().c_str());
        snprintf(e.text,sizeof(e.text),"%s",d->getName().c_str());post(e);
    }
    void onScanEnd(const NimBLEScanResults&,int) override {event(BleEventKind::ScanDone,"BLE scan complete");}
} scanCallbacks;
class CompanionCallbacks:public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c,NimBLEConnInfo& info) override {
        if(!info.isEncrypted() || !info.isAuthenticated())return;
        std::string value=c->getValue();
        while(!value.empty() && (value.back()=='\r' || value.back()=='\n' || value.back()==' '))value.pop_back();
        if(value=="status" || value=="help")statusRequested=true;
    }
} companionCallbacks;
void notification(NimBLERemoteCharacteristic*,uint8_t* data,size_t size,bool) {
    if(!size || size>32)return;
    BleEvent e{};e.kind=BleEventKind::Hid;e.length=size;std::memcpy(e.bytes,data,size);post(e);
}
bool useKeyboard() {
    auto* hid=keyboard->getService(NimBLEUUID(uint16_t(0x1812)));
    if(!hid)return false;
    NimBLERemoteCharacteristic* input=hid->getCharacteristic(NimBLEUUID(uint16_t(0x2a22)));
    auto* protocol=hid->getCharacteristic(NimBLEUUID(uint16_t(0x2a4e)));
    deck::HidKeyboardLayout layout;
    bool found=false;
    if(input && input->canNotify() && protocol) {
        uint8_t boot=0;found=protocol->writeValue(&boot,1,protocol->canWrite());
    }
    if(!found) {
        auto* map=hid->getCharacteristic(NimBLEUUID(uint16_t(0x2a4b)));
        if(!map)return false;
        auto descriptor=map->readValue();
        for(auto* c:hid->getCharacteristics(true)) {
            if(c->getUUID()!=NimBLEUUID(uint16_t(0x2a4d)) || !c->canNotify())continue;
            auto* ref=c->getDescriptor(NimBLEUUID(uint16_t(0x2908)));if(!ref)continue;
            auto value=ref->readValue();
            if(value.size()!=2 || value[1]!=1)continue;
            if(deck::keyboardLayout(descriptor.data(),descriptor.size(),value[0],layout)){input=c;found=true;break;}
        }
        if(found && protocol){uint8_t report=1;if(!protocol->writeValue(&report,1,protocol->canWrite()))return false;}
    }
    if(!found || !input)return false;
    BleEvent e{};e.kind=BleEventKind::Layout;e.bytes[0]=layout.modifier;e.bytes[1]=layout.keys;e.bytes[2]=layout.count;e.bytes[3]=layout.bytes;e.bytes[4]=layout.bitmap;e.bytes[5]=layout.firstUsage;
    if(!post(e) || !input->subscribe(true,notification))return false;
    auto address=keyboard->getConnInfo().getIdAddress();
    e={};e.kind=BleEventKind::RememberKeyboard;e.addressType=address.getType();
    snprintf(e.address,sizeof(e.address),"%s",address.toString().c_str());post(e);
    event(BleEventKind::Keyboard,"BLE keyboard ready",1);return true;
}
void worker(void*) {
    bool enabled=false,needsKeyboard=false;uint32_t securityAt=0;
    std::string status="Handheld OS 0.1\n",notificationText;
    size_t notificationOffset=0;uint16_t notificationHandle=BLE_HS_CONN_HANDLE_NONE;uint32_t notificationAt=0;
    for(;;) {
        BleRequest a{};
        if(xQueueReceive(bleRequests,&a,pdMS_TO_TICKS(20))==pdTRUE) {
            if(a.kind==BleCommand::Enable && !enabled) {
                if(!NimBLEDevice::init("Handheld OS")){event(BleEventKind::State,"BLE initialization failed",0);continue;}
                enabled=true;NimBLEDevice::setSecurityAuth(true,true,true);NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
                server=NimBLEDevice::createServer();server->setCallbacks(&serverCallbacks,false);server->advertiseOnDisconnect(false);
                auto* device=server->createService(uint16_t(0x180a));
                device->createCharacteristic(uint16_t(0x2a26),NIMBLE_PROPERTY::READ)->setValue("Handheld OS 0.1");
                auto* battery=server->createService(uint16_t(0x180f));
                batteryCharacteristic=battery->createCharacteristic(uint16_t(0x2a19),NIMBLE_PROPERTY::READ);
                auto* service=server->createService(UartService);
                statusCharacteristic=service->createCharacteristic(UartTx,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_AUTHEN|NIMBLE_PROPERTY::NOTIFY);
                service->createCharacteristic(UartRx,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_NR|NIMBLE_PROPERTY::WRITE_AUTHEN)->setCallbacks(&companionCallbacks);
                server->start();
                auto* adv=NimBLEDevice::getAdvertising();adv->enableScanResponse(true);adv->setName("Handheld OS");adv->addServiceUUID(UartService);
                auto* scan=NimBLEDevice::getScan();scan->setScanCallbacks(&scanCallbacks);scan->setActiveScan(true);scan->setMaxResults(0);
                scan->setInterval(100);scan->setWindow(40);
                event(BleEventKind::State,"BLE ready; P makes phone visible",1);
            } else if(a.kind==BleCommand::Disable && enabled) {
                enabled=false;needsKeyboard=false;pairingUntil=0;phoneHandle=BLE_HS_CONN_HANDLE_NONE;
                notificationText.clear();NimBLEDevice::deinit(true);server=nullptr;keyboard=nullptr;statusCharacteristic=batteryCharacteristic=nullptr;
                event(BleEventKind::State,"BLE off",0);
            } else if(!enabled)continue;
            else if(a.kind==BleCommand::Advertise) {
                if(server->getConnectedCount()){event(BleEventKind::Notice,"Phone already connected");continue;}
                pairingUntil=millis()+120000;
                bool ok=NimBLEDevice::getAdvertising()->start(120000);
                if(!ok)pairingUntil=0;
                event(BleEventKind::Advertising,ok?"Visible 2 min: use BLE UART app":"Phone visibility failed",ok);
            } else if(a.kind==BleCommand::Scan) {
                if(needsKeyboard){event(BleEventKind::Notice,"Finish keyboard pairing first");continue;}
                auto* scan=NimBLEDevice::getScan();scan->stop();scan->clearResults();
                event(BleEventKind::ScanStart,"Scanning BLE devices...");
                if(!scan->start(8000))event(BleEventKind::ScanDone,"BLE scan failed");
            } else if(a.kind==BleCommand::Connect) {
                if(!deck::validBleAddress(a.address)){event(BleEventKind::Notice,"No saved keyboard address");continue;}
                NimBLEDevice::getScan()->stop();
                if(!keyboard){keyboard=NimBLEDevice::createClient();if(keyboard)keyboard->setClientCallbacks(&clientCallbacks,false);}
                if(!keyboard){event(BleEventKind::Keyboard,"BLE connection slots unavailable",0);continue;}
                if(keyboard->isConnected())keyboard->disconnect();
                keyboard->setConnectTimeout(10000);keyboard->setConnectRetries(0);
                event(BleEventKind::Keyboard,"Connecting keyboard...",0);
                if(!keyboard->connect(NimBLEAddress(std::string(a.address),a.addressType))) {event(BleEventKind::Keyboard,"Keyboard unavailable; put in pairing mode",0);continue;}
                securityAt=millis();needsKeyboard=true;
                if(!keyboard->secureConnection(true)){needsKeyboard=false;keyboard->disconnect();event(BleEventKind::Keyboard,"Keyboard security could not start",0);}
            } else if(a.kind==BleCommand::Disconnect || a.kind==BleCommand::ForgetKeyboard || a.kind==BleCommand::ForgetAll) {
                needsKeyboard=false;if(keyboard && keyboard->isConnected())keyboard->disconnect();
                if(a.kind==BleCommand::ForgetKeyboard && deck::validBleAddress(a.address))NimBLEDevice::deleteBond(NimBLEAddress(std::string(a.address),a.addressType));
                if(a.kind==BleCommand::ForgetAll){for(auto handle:server->getPeerDevices())server->disconnect(handle);NimBLEDevice::deleteAllBonds();}
                event(BleEventKind::Keyboard,"Keyboard disconnected",0);
            } else if(a.kind==BleCommand::CancelPair) {
                auto* client=NimBLEDevice::getClientByHandle(a.handle);
                if(client)client->disconnect();else server->disconnect(a.handle);
            } else if(a.kind==BleCommand::Confirm || a.kind==BleCommand::Passkey) {
                auto* client=NimBLEDevice::getClientByHandle(a.handle);
                auto info=client?client->getConnInfo():server->getPeerInfoByHandle(a.handle);
                if(info.getConnHandle()!=a.handle)continue;
                if(a.kind==BleCommand::Confirm)NimBLEDevice::injectConfirmPasskey(info,a.value);
                else NimBLEDevice::injectPassKey(info,a.value);
            } else if(a.kind==BleCommand::Status) {
                status=a.text;statusCharacteristic->setValue(status);batteryCharacteristic->setValue(&a.battery,1);
            }
        }
        if(enabled && needsKeyboard && keyboard) {
            if(!keyboard->isConnected())needsKeyboard=false;
            else if(keyboard->getConnInfo().isEncrypted() && keyboard->getConnInfo().isAuthenticated()) {
                needsKeyboard=false;if(!useKeyboard()){keyboard->disconnect();event(BleEventKind::Keyboard,"Unsupported keyboard HID report",0);}
            } else if(uint32_t(millis()-securityAt)>60000) {needsKeyboard=false;keyboard->disconnect();event(BleEventKind::Keyboard,"Keyboard pairing timed out",0);}
        }
        if(enabled && statusRequested.exchange(false) && phoneHandle!=BLE_HS_CONN_HANDLE_NONE) {
            notificationText=status;notificationOffset=0;notificationHandle=phoneHandle;notificationAt=millis();
        }
        if(enabled && !notificationText.empty()) {
            if(notificationHandle!=phoneHandle || uint32_t(millis()-notificationAt)>5000)notificationText.clear();
            else {
                auto info=server->getPeerInfoByHandle(notificationHandle);
                if(!info.isEncrypted() || !info.isAuthenticated())notificationText.clear();
                else {
                    size_t n=std::min(size_t(20),notificationText.size()-notificationOffset);
                    if(statusCharacteristic->notify(reinterpret_cast<const uint8_t*>(notificationText.data()+notificationOffset),n,notificationHandle))notificationOffset+=n;
                    if(notificationOffset==notificationText.size())notificationText.clear();
                }
            }
        }
        if(enabled && phoneHandle!=BLE_HS_CONN_HANDLE_NONE && uint32_t(millis()-phoneConnectedAt)>60000) {
            auto info=server->getPeerInfoByHandle(phoneHandle);
            if(!info.isEncrypted() || !info.isAuthenticated())server->disconnect(info);
        }
        if(enabled && pairingUntil && !windowOpen()){pairingUntil=0;event(BleEventKind::Advertising,"Phone visibility ended",0);}
    }
}
}
bool startBleTask() {
    bleRequests=xQueueCreate(8,sizeof(BleRequest));bleEvents=xQueueCreate(48,sizeof(BleEvent));
    if(!bleRequests || !bleEvents)return false;
    return xTaskCreatePinnedToCore(worker,"ble-setup",8192,nullptr,1,nullptr,0)==pdPASS;
}
bool bleRequest(BleCommand kind,const char* address,uint8_t addressType,uint32_t value,uint16_t handle,const char* text,uint8_t battery) {
    if(!bleRequests)return false;
    BleRequest a{};a.kind=kind;a.value=value;a.handle=handle;a.addressType=addressType;a.battery=battery;
    snprintf(a.address,sizeof(a.address),"%s",address);snprintf(a.text,sizeof(a.text),"%s",text);
    return xQueueSend(bleRequests,&a,0)==pdTRUE;
}
