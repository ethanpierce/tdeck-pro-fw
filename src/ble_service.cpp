#include "ble_service.h"
#include "platform.h"
#include <connectivity.h>
#include <radio_gate.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <string>
#include <esp_random.h>
#include <esp_bt.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
extern "C" {
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <host/ble_uuid.h>
#include <host/ble_hs_adv.h>
#include <host/ble_hs_mbuf.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
void ble_store_config_init(void);
}
QueueHandle_t bleRequests=nullptr,bleEvents=nullptr;
std::atomic<uint32_t> droppedBleEvents{0};
namespace {
constexpr uint16_t None=BLE_HS_CONN_HANDLE_NONE;
const ble_uuid16_t firmwareUuid=BLE_UUID16_INIT(0x2a26),batteryUuid=BLE_UUID16_INIT(0x2a19);
const ble_uuid16_t infoUuid=BLE_UUID16_INIT(0x180a),batteryServiceUuid=BLE_UUID16_INIT(0x180f),hidUuid=BLE_UUID16_INIT(0x1812);
const ble_uuid128_t uartService=BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
const ble_uuid128_t uartRx=BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
const ble_uuid128_t uartTx=BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);
std::atomic<bool> synced{false};
deck::RadioGate gate;
std::atomic<bool> hostActive{false};
bool enabled=false,connectingKeyboard=false,phoneSubscribed=false;
uint8_t ownAddressType=BLE_OWN_ADDR_PUBLIC,batteryLevel=0;
uint16_t phone=None,keyboard=None,statusHandle=0;
uint32_t pairingUntil=0,phoneAt=0,keyboardAt=0;
std::string status="Handheld OS 0.1\n",notification;
size_t notificationOffset=0;uint16_t notificationPhone=None;uint32_t notificationAt=0;
struct PairAction {uint16_t handle=None;uint8_t action=0;uint32_t at=0;};
std::array<PairAction,3> pairs{};
struct HidState {
    uint16_t start=0,end=0,input=0,cccd=0,reference=0,protocol=0,mapHandle=0;
    std::array<ble_gatt_chr,32> characteristics{};size_t count=0,candidate=0;
    std::array<uint8_t,1024> map{};size_t mapSize=0;
    deck::HidKeyboardLayout layout{};bool discovering=false,ready=false,boot=false;
} hid;
ble_npl_event workEvent{};SemaphoreHandle_t workComplete=nullptr;
enum class WorkKind {Request,Poll,Shutdown};
WorkKind workKind=WorkKind::Poll;BleRequest workRequest{};
int gapEvent(ble_gap_event*,void*);
void beginReportMode();void nextReport();void subscribeInput();
bool post(BleEvent e) {
    if(!gate.allowed() && (e.kind==BleEventKind::Hid || e.kind==BleEventKind::Device ||
       e.kind==BleEventKind::Layout || e.kind==BleEventKind::RememberKeyboard || e.kind==BleEventKind::ScanStart ||
       e.kind==BleEventKind::PairConfirm || e.kind==BleEventKind::PairEntry || e.kind==BleEventKind::PairDisplay ||
       ((e.kind==BleEventKind::State || e.kind==BleEventKind::Phone || e.kind==BleEventKind::Keyboard || e.kind==BleEventKind::Advertising) && e.value)))return true;
    if(bleEvents && xQueueSend(bleEvents,&e,0)==pdTRUE)return true;
    droppedBleEvents.fetch_add(1,std::memory_order_relaxed);return false;
}
void event(BleEventKind kind,const std::string& text="",uint32_t value=0,uint16_t handle=0) {
    BleEvent e{};e.kind=kind;e.value=value;e.handle=handle;snprintf(e.text,sizeof(e.text),"%s",text.c_str());post(e);
}
void addressText(const ble_addr_t& address,char* text,size_t size) {
    snprintf(text,size,"%02x:%02x:%02x:%02x:%02x:%02x",address.val[5],address.val[4],address.val[3],address.val[2],address.val[1],address.val[0]);
}
bool parseAddress(const char* text,uint8_t type,ble_addr_t& out) {
    if(!deck::validBleAddress(text) || type>BLE_ADDR_RANDOM_ID)return false;
    unsigned b[6];if(sscanf(text,"%02x:%02x:%02x:%02x:%02x:%02x",b+5,b+4,b+3,b+2,b+1,b)!=6)return false;
    out.type=type;for(unsigned i=0;i<6;++i)out.val[i]=uint8_t(b[i]);return true;
}
bool secure(uint16_t handle) {
    ble_gap_conn_desc d{};return handle!=None && !ble_gap_conn_find(handle,&d) && d.sec_state.encrypted && d.sec_state.authenticated;
}
bool windowOpen(){return pairingUntil && int32_t(pairingUntil-platform::millis())>0;}
bool bonded(const ble_addr_t& address) {
    // Read only: a full bond store is never silently evicted to accept a peer.
    ble_store_key_sec key{};ble_store_value_sec value{};key.peer_addr=address;
    return !ble_store_read_peer_sec(&key,&value);
}
void terminate(uint16_t handle){if(handle!=None)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);}
void clearPair(uint16_t handle){for(auto& p:pairs)if(p.handle==handle)p={};}
void hidFailure(const char* text="Unsupported keyboard HID report") {
    hid.discovering=false;hid.ready=false;event(BleEventKind::Keyboard,text,0);terminate(keyboard);
}
bool currentKeyboard(uint16_t handle){return gate.allowed() && handle==keyboard && handle!=None && secure(handle);}
int subscribed(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr*,void*) {
    if(!currentKeyboard(handle))return 0;
    if(error->status){hidFailure("Keyboard notification subscription failed");return 0;}
    BleEvent e{};e.kind=BleEventKind::Layout;e.bytes[0]=hid.layout.modifier;e.bytes[1]=hid.layout.keys;e.bytes[2]=hid.layout.count;e.bytes[3]=hid.layout.bytes;e.bytes[4]=hid.layout.bitmap;e.bytes[5]=hid.layout.firstUsage;
    if(!post(e)){hidFailure("BLE event queue full");return 0;}
    hid.discovering=false;hid.ready=true;
    ble_gap_conn_desc d{};
    if(!ble_gap_conn_find(handle,&d)){e={};e.kind=BleEventKind::RememberKeyboard;e.addressType=d.peer_id_addr.type;addressText(d.peer_id_addr,e.address,sizeof(e.address));post(e);}
    event(BleEventKind::Keyboard,"BLE keyboard ready",1);return 0;
}
void subscribeInput() {
    if(!hid.input || !hid.cccd){hidFailure();return;}
    const uint8_t notify[]={1,0};
    if(ble_gattc_write_flat(keyboard,hid.cccd,notify,sizeof(notify),subscribed,nullptr))hidFailure("Keyboard notification subscription failed");
}
int protocolWritten(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr*,void*) {
    if(!currentKeyboard(handle))return 0;
    if(error->status){if(hid.boot)beginReportMode();else hidFailure("Keyboard protocol selection failed");}
    else subscribeInput();
    return 0;
}
void setProtocolAndSubscribe() {
    if(!hid.protocol){subscribeInput();return;}
    const uint8_t mode=hid.boot?0:1;
    auto c=std::find_if(hid.characteristics.begin(),hid.characteristics.begin()+hid.count,[](const ble_gatt_chr& c){return ble_uuid_u16(&c.uuid.u)==0x2a4e;});
    int result;
    if(c!=hid.characteristics.begin()+hid.count && (c->properties & BLE_GATT_CHR_PROP_WRITE))
        result=ble_gattc_write_flat(keyboard,hid.protocol,&mode,1,protocolWritten,nullptr);
    else {result=ble_gattc_write_no_rsp_flat(keyboard,hid.protocol,&mode,1);if(!result)subscribeInput();}
    if(result){if(hid.boot)beginReportMode();else hidFailure("Keyboard protocol selection failed");}
}
int referenceRead(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr* attr,void*) {
    if(!currentKeyboard(handle))return 0;
    uint8_t ref[2];
    if(!error->status && attr && OS_MBUF_PKTLEN(attr->om)==2 && !os_mbuf_copydata(attr->om,0,2,ref) && ref[1]==1 && deck::keyboardLayout(hid.map.data(),hid.mapSize,ref[0],hid.layout))setProtocolAndSubscribe();
    else nextReport();
    return 0;
}
int descriptors(uint16_t handle,const ble_gatt_error* error,uint16_t,const ble_gatt_dsc* d,void*) {
    if(!currentKeyboard(handle))return 0;
    if(!error->status && d){const auto uuid=ble_uuid_u16(&d->uuid.u);if(uuid==0x2902)hid.cccd=d->handle;else if(uuid==0x2908)hid.reference=d->handle;return 0;}
    if(error->status!=BLE_HS_EDONE){if(hid.boot)beginReportMode();else nextReport();return 0;}
    if(hid.boot){if(hid.cccd)setProtocolAndSubscribe();else beginReportMode();}
    else if(hid.reference && hid.cccd){if(ble_gattc_read(keyboard,hid.reference,referenceRead,nullptr))nextReport();}
    else nextReport();
    return 0;
}
void discoverInput(size_t index) {
    hid.input=hid.characteristics[index].val_handle;hid.cccd=hid.reference=0;
    const uint16_t end=index+1<hid.count?uint16_t(hid.characteristics[index+1].def_handle-1):hid.end;
    if(end<=hid.input || ble_gattc_disc_all_dscs(keyboard,hid.input,end,descriptors,nullptr)){if(hid.boot)beginReportMode();else nextReport();}
}
void nextReport() {
    while(hid.candidate<hid.count){const size_t i=hid.candidate++;const auto& c=hid.characteristics[i];
        if(ble_uuid_u16(&c.uuid.u)==0x2a4d && (c.properties & BLE_GATT_CHR_PROP_NOTIFY)){discoverInput(i);return;}}
    hidFailure();
}
int mapRead(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr* attr,void*) {
    if(!currentKeyboard(handle))return 0;
    if(!error->status && attr){const size_t length=OS_MBUF_PKTLEN(attr->om);
        if(attr->offset!=hid.mapSize || length>hid.map.size()-hid.mapSize || os_mbuf_copydata(attr->om,0,length,hid.map.data()+hid.mapSize)){hidFailure("Keyboard report map too large");return BLE_HS_EAPP;}
        hid.mapSize+=length;return 0;
    }
    if(error->status==BLE_HS_EDONE && hid.mapSize){hid.candidate=0;nextReport();}else hidFailure("Keyboard report map read failed");return 0;
}
void beginReportMode() {
    hid.boot=false;hid.mapSize=0;hid.candidate=0;
    if(!hid.mapHandle || ble_gattc_read_long(keyboard,hid.mapHandle,0,mapRead,nullptr))hidFailure("Keyboard report map unavailable");
}
int characteristics(uint16_t handle,const ble_gatt_error* error,const ble_gatt_chr* c,void*) {
    if(!currentKeyboard(handle))return 0;
    if(!error->status && c){if(hid.count>=hid.characteristics.size()){hidFailure("Keyboard HID service too large");return BLE_HS_EAPP;}hid.characteristics[hid.count++]=*c;return 0;}
    if(error->status!=BLE_HS_EDONE){hidFailure("Keyboard HID discovery failed");return 0;}
    size_t boot=hid.count;
    for(size_t i=0;i<hid.count;++i){const auto& chr=hid.characteristics[i];const auto uuid=ble_uuid_u16(&chr.uuid.u);
        if(uuid==0x2a4e && (chr.properties & (BLE_GATT_CHR_PROP_WRITE|BLE_GATT_CHR_PROP_WRITE_NO_RSP)))hid.protocol=chr.val_handle;
        if(uuid==0x2a4b)hid.mapHandle=chr.val_handle;
        if(uuid==0x2a22 && (chr.properties & BLE_GATT_CHR_PROP_NOTIFY))boot=i;
    }
    if(boot<hid.count && hid.protocol){hid.boot=true;hid.layout={};discoverInput(boot);}else beginReportMode();return 0;
}
int services(uint16_t handle,const ble_gatt_error* error,const ble_gatt_svc* svc,void*) {
    if(!currentKeyboard(handle))return 0;
    if(!error->status && svc){if(!hid.start){hid.start=svc->start_handle;hid.end=svc->end_handle;}return 0;}
    if(error->status!=BLE_HS_EDONE || !hid.start || ble_gattc_disc_all_chrs(keyboard,hid.start,hid.end,characteristics,nullptr))hidFailure("Keyboard HID service unavailable");
    return 0;
}
void discoverKeyboard() {
    if(hid.discovering || hid.ready)return;
    hid={};hid.discovering=true;keyboardAt=platform::millis();
    if(ble_gattc_disc_svc_by_uuid(keyboard,&hidUuid.u,services,nullptr))hidFailure("Keyboard HID discovery failed");
}
int access(uint16_t handle,uint16_t,ble_gatt_access_ctxt* context,void* argument) {
    if(!gate.allowed())return BLE_ATT_ERR_UNLIKELY;
    const auto kind=uintptr_t(argument);
    if(kind==1)return os_mbuf_append(context->om,"Handheld OS 0.1",15)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    if(kind==2)return os_mbuf_append(context->om,&batteryLevel,1)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    if(handle!=phone || !secure(handle))return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if(kind==3 && context->op==BLE_GATT_ACCESS_OP_READ_CHR)return os_mbuf_append(context->om,status.data(),status.size())?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    if(kind==4 && context->op==BLE_GATT_ACCESS_OP_WRITE_CHR){
        const size_t length=OS_MBUF_PKTLEN(context->om);if(length>32)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        char bytes[33]{};if(os_mbuf_copydata(context->om,0,length,bytes))return BLE_ATT_ERR_UNLIKELY;
        std::string command(bytes,length);while(!command.empty() && (command.back()=='\r' || command.back()=='\n' || command.back()==' '))command.pop_back();
        if(command=="status" || command=="help"){notification=status;notificationOffset=0;notificationPhone=phone;notificationAt=platform::millis();}
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
ble_gatt_chr_def infoCharacteristics[2]{},batteryCharacteristics[2]{},uartCharacteristics[3]{};
ble_gatt_svc_def serviceDefinitions[4]{};
void configureServices() {
    std::memset(infoCharacteristics,0,sizeof(infoCharacteristics));std::memset(batteryCharacteristics,0,sizeof(batteryCharacteristics));std::memset(uartCharacteristics,0,sizeof(uartCharacteristics));std::memset(serviceDefinitions,0,sizeof(serviceDefinitions));
    infoCharacteristics[0].uuid=&firmwareUuid.u;infoCharacteristics[0].access_cb=access;infoCharacteristics[0].arg=reinterpret_cast<void*>(1);infoCharacteristics[0].flags=BLE_GATT_CHR_F_READ;
    batteryCharacteristics[0].uuid=&batteryUuid.u;batteryCharacteristics[0].access_cb=access;batteryCharacteristics[0].arg=reinterpret_cast<void*>(2);batteryCharacteristics[0].flags=BLE_GATT_CHR_F_READ;
    uartCharacteristics[0].uuid=&uartTx.u;uartCharacteristics[0].access_cb=access;uartCharacteristics[0].arg=reinterpret_cast<void*>(3);uartCharacteristics[0].flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_NOTIFY;uartCharacteristics[0].val_handle=&statusHandle;
    uartCharacteristics[1].uuid=&uartRx.u;uartCharacteristics[1].access_cb=access;uartCharacteristics[1].arg=reinterpret_cast<void*>(4);uartCharacteristics[1].flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_AUTHEN;
    serviceDefinitions[0].type=BLE_GATT_SVC_TYPE_PRIMARY;serviceDefinitions[0].uuid=&infoUuid.u;serviceDefinitions[0].characteristics=infoCharacteristics;
    serviceDefinitions[1].type=BLE_GATT_SVC_TYPE_PRIMARY;serviceDefinitions[1].uuid=&batteryServiceUuid.u;serviceDefinitions[1].characteristics=batteryCharacteristics;
    serviceDefinitions[2].type=BLE_GATT_SVC_TYPE_PRIMARY;serviceDefinitions[2].uuid=&uartService.u;serviceDefinitions[2].characteristics=uartCharacteristics;
}
void pairing(ble_gap_event* g) {
    const uint16_t handle=g->passkey.conn_handle;const uint8_t action=g->passkey.params.action;
    if(handle!=phone && handle!=keyboard){terminate(handle);return;}
    PairAction* slot=nullptr;for(auto& p:pairs)if(p.handle==handle || p.handle==None){slot=&p;break;}
    if(!slot){terminate(handle);return;}
    *slot={handle,action,platform::millis()};
    BleEvent e{};e.handle=handle;ble_gap_conn_desc d{};if(!ble_gap_conn_find(handle,&d))addressText(d.peer_ota_addr,e.address,sizeof(e.address));
    ble_sm_io io{};io.action=action;
    if(action==BLE_SM_IOACT_NUMCMP){e.kind=BleEventKind::PairConfirm;e.value=g->passkey.params.numcmp;}
    else if(action==BLE_SM_IOACT_INPUT)e.kind=BleEventKind::PairEntry;
    else if(action==BLE_SM_IOACT_DISP){e.kind=BleEventKind::PairDisplay;e.value=100000+esp_random()%900000;io.passkey=e.value;}
    else {clearPair(handle);terminate(handle);return;}
    if(!post(e)){clearPair(handle);terminate(handle);return;}
    if(action==BLE_SM_IOACT_DISP && ble_sm_inject_io(handle,&io)){clearPair(handle);terminate(handle);}
}
void scanResult(const ble_gap_disc_desc& result) {
    ble_hs_adv_fields fields{};if(ble_hs_adv_parse_fields(&fields,result.data,result.length_data))return;
    bool candidate=fields.appearance_is_present && fields.appearance==0x03c1;
    for(unsigned i=0;i<fields.num_uuids16;++i)if(fields.uuids16[i].value==0x1812)candidate=true;
    if(!candidate && !fields.name_len)return;
    BleEvent e{};e.kind=BleEventKind::Device;e.value=candidate;e.handle=uint16_t(int16_t(result.rssi));e.addressType=result.addr.type;
    addressText(result.addr,e.address,sizeof(e.address));if(fields.name)std::memcpy(e.text,fields.name,std::min(size_t(fields.name_len),sizeof(e.text)-1));post(e);
}
int gapEvent(ble_gap_event* g,void*) {
    if(!gate.allowed() && g->type!=BLE_GAP_EVENT_DISCONNECT){
        if(g->type==BLE_GAP_EVENT_CONNECT && !g->connect.status)terminate(g->connect.conn_handle);
        return g->type==BLE_GAP_EVENT_REPEAT_PAIRING?BLE_GAP_REPEAT_PAIRING_IGNORE:0;
    }
    switch(g->type){
    case BLE_GAP_EVENT_CONNECT:{
        if(g->connect.status){if(connectingKeyboard){connectingKeyboard=false;event(BleEventKind::Keyboard,"Keyboard unavailable; put in pairing mode",0);}break;}
        ble_gap_conn_desc d{};if(ble_gap_conn_find(g->connect.conn_handle,&d)){terminate(g->connect.conn_handle);break;}
        if(d.role==BLE_GAP_ROLE_MASTER){connectingKeyboard=false;if(keyboard!=None){terminate(d.conn_handle);break;}keyboard=d.conn_handle;keyboardAt=platform::millis();hid={};}
        else {if(phone!=None || (!windowOpen() && !bonded(d.peer_id_addr))){terminate(d.conn_handle);break;}phone=d.conn_handle;phoneAt=platform::millis();pairingUntil=0;phoneSubscribed=false;event(BleEventKind::Phone,"Phone connected; awaiting security",0);}
        if(ble_gap_security_initiate(d.conn_handle))terminate(d.conn_handle);
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:{
        const auto handle=g->disconnect.conn.conn_handle;clearPair(handle);
        if(handle==phone){phone=None;phoneSubscribed=false;notification.clear();event(BleEventKind::Phone,"Phone disconnected",0);event(BleEventKind::PairDone,"Phone disconnected",0,handle);}
        if(handle==keyboard){keyboard=None;hid={};event(BleEventKind::Keyboard,"Keyboard disconnected",0);event(BleEventKind::PairDone,"Keyboard disconnected",0,handle);}break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:{
        const auto handle=g->enc_change.conn_handle;if(handle!=phone && handle!=keyboard)break;
        clearPair(handle);const bool ok=!g->enc_change.status && secure(handle);
        event(BleEventKind::PairDone,ok?"BLE pairing secured":"BLE pairing failed",ok,handle);
        if(!ok)terminate(handle);else if(handle==phone)event(BleEventKind::Phone,"Phone paired and encrypted",1);else discoverKeyboard();break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:pairing(g);break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:return BLE_GAP_REPEAT_PAIRING_IGNORE; // Explicit Forget required; never discard an existing bond implicitly.
    case BLE_GAP_EVENT_DISC:scanResult(g->disc);break;
    case BLE_GAP_EVENT_DISC_COMPLETE:event(BleEventKind::ScanDone,"BLE scan complete");break;
    case BLE_GAP_EVENT_ADV_COMPLETE:pairingUntil=0;event(BleEventKind::Advertising,"Phone visibility ended",0);break;
    case BLE_GAP_EVENT_SUBSCRIBE:if(g->subscribe.conn_handle==phone && g->subscribe.attr_handle==statusHandle)phoneSubscribed=g->subscribe.cur_notify;break;
    case BLE_GAP_EVENT_NOTIFY_RX:{
        const size_t size=OS_MBUF_PKTLEN(g->notify_rx.om);
        if(g->notify_rx.conn_handle!=keyboard || !hid.ready || g->notify_rx.attr_handle!=hid.input || !size || size>32 || !secure(keyboard))break;
        BleEvent e{};e.kind=BleEventKind::Hid;e.length=uint8_t(size);if(!os_mbuf_copydata(g->notify_rx.om,0,size,e.bytes))post(e);break;
    }
    default:break;
    }return 0;
}
void advertise() {
    if(phone!=None){event(BleEventKind::Notice,"Phone already connected");return;}
    ble_gap_adv_stop();
    ble_hs_adv_fields fields{};fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;fields.uuids128=&uartService;fields.num_uuids128=1;fields.uuids128_is_complete=1;
    ble_hs_adv_fields response{};response.name=reinterpret_cast<const uint8_t*>("Handheld OS");response.name_len=11;response.name_is_complete=1;
    ble_gap_adv_params params{};params.conn_mode=BLE_GAP_CONN_MODE_UND;params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    const bool ok=!ble_gap_adv_set_fields(&fields) && !ble_gap_adv_rsp_set_fields(&response) && !ble_gap_adv_start(ownAddressType,nullptr,120000,&params,gapEvent,nullptr);
    pairingUntil=ok?platform::millis()+120000:0;event(BleEventKind::Advertising,ok?"Visible 2 min: use BLE UART app":"Phone visibility failed",ok);
}
void handleRequest(const BleRequest& a) {
    if(!gate.accepts(a.ticket))return;
    if(!synced.load()){event(BleEventKind::Notice,"BLE is starting; try again");return;}
    if(a.kind==BleCommand::Advertise)advertise();
    else if(a.kind==BleCommand::Scan){
        if(connectingKeyboard || (keyboard!=None && !hid.ready)){event(BleEventKind::Notice,"Finish keyboard pairing first");return;}
        ble_gap_disc_cancel();ble_gap_disc_params p{};p.itvl=160;p.window=64;p.passive=0;p.filter_duplicates=0;
        event(BleEventKind::ScanStart,"Scanning BLE devices...");if(ble_gap_disc(ownAddressType,8000,&p,gapEvent,nullptr))event(BleEventKind::ScanDone,"BLE scan failed");
    } else if(a.kind==BleCommand::Connect){
        ble_addr_t address{};if(!parseAddress(a.address,a.addressType,address)){event(BleEventKind::Notice,"No saved keyboard address");return;}
        if(keyboard!=None || connectingKeyboard){event(BleEventKind::Notice,"Disconnect keyboard before reconnecting");return;}
        ble_gap_disc_cancel();keyboardAt=platform::millis();connectingKeyboard=true;event(BleEventKind::Keyboard,"Connecting keyboard...",0);
        if(ble_gap_connect(ownAddressType,&address,10000,nullptr,gapEvent,nullptr)){connectingKeyboard=false;event(BleEventKind::Keyboard,"Keyboard unavailable; put in pairing mode",0);}
    } else if(a.kind==BleCommand::Disconnect || a.kind==BleCommand::ForgetKeyboard || a.kind==BleCommand::ForgetAll){
        if(connectingKeyboard){ble_gap_conn_cancel();connectingKeyboard=false;}terminate(keyboard);
        if(a.kind==BleCommand::ForgetKeyboard){ble_addr_t address{};if(parseAddress(a.address,a.addressType,address))ble_store_util_delete_peer(&address);}
        if(a.kind==BleCommand::ForgetAll){terminate(phone);ble_store_clear();}
        event(BleEventKind::Keyboard,"Keyboard disconnected",0);
    } else if(a.kind==BleCommand::CancelPair){clearPair(a.handle);terminate(a.handle);}
    else if(a.kind==BleCommand::Confirm || a.kind==BleCommand::Passkey){
        for(auto& p:pairs)if(p.handle==a.handle){ble_sm_io io{};io.action=p.action;
            if(a.kind==BleCommand::Confirm && p.action==BLE_SM_IOACT_NUMCMP)io.numcmp_accept=bool(a.value);
            else if(a.kind==BleCommand::Passkey && p.action==BLE_SM_IOACT_INPUT && a.value<=999999)io.passkey=a.value;
            else break;
            if(ble_sm_inject_io(a.handle,&io))terminate(a.handle);
            p={};break;
        }
    } else if(a.kind==BleCommand::Status){status.assign(a.text,strnlen(a.text,sizeof(a.text)));batteryLevel=a.battery;}
}
void pollHost() {
    if(!gate.allowed())return;
    const auto now=platform::millis();
    for(auto& p:pairs)if(p.handle!=None && uint32_t(now-p.at)>60000){terminate(p.handle);p={};}
    if(phone!=None && uint32_t(now-phoneAt)>60000 && !secure(phone))terminate(phone);
    if(keyboard!=None && !hid.ready && uint32_t(now-keyboardAt)>60000)hidFailure("Keyboard pairing timed out");
    if(connectingKeyboard && uint32_t(now-keyboardAt)>12000){ble_gap_conn_cancel();connectingKeyboard=false;event(BleEventKind::Keyboard,"Keyboard connection timed out",0);}
    if(pairingUntil && !windowOpen()){pairingUntil=0;ble_gap_adv_stop();event(BleEventKind::Advertising,"Phone visibility ended",0);}
    if(!notification.empty()){
        if(notificationPhone!=phone || !secure(phone) || uint32_t(now-notificationAt)>5000)notification.clear();
        else if(phoneSubscribed){const size_t size=std::min(size_t(20),notification.size()-notificationOffset);
            auto* bytes=ble_hs_mbuf_from_flat(notification.data()+notificationOffset,size);
            if(bytes && !ble_gatts_notify_custom(phone,statusHandle,bytes))notificationOffset+=size;
            if(notificationOffset==notification.size())notification.clear();
        }
    }
}
void workCallback(ble_npl_event*) {
    if(workKind==WorkKind::Request)handleRequest(workRequest);
    else if(workKind==WorkKind::Poll)pollHost();
    else {pairingUntil=0;ble_gap_adv_stop();ble_gap_disc_cancel();ble_gap_conn_cancel();terminate(phone);terminate(keyboard);notification.clear();}
    xSemaphoreGive(workComplete);
}
void dispatch(WorkKind kind,const BleRequest* request=nullptr) {
    workKind=kind;if(request)workRequest=*request;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&workEvent);xSemaphoreTake(workComplete,portMAX_DELAY);
}
void hostTask(void*){nimble_port_run();nimble_port_freertos_deinit();}
void hostSync(){if(ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0,&ownAddressType)){event(BleEventKind::State,"BLE identity initialization failed",0);return;}synced=true;if(gate.allowed())event(BleEventKind::State,"BLE ready; P makes phone visible",1);}
void hostReset(int) {
    synced=false;
    for(const auto& p:pairs)if(p.handle!=None)event(BleEventKind::PairDone,"BLE host reset",0,p.handle);
    pairs={};phone=keyboard=None;hid={};connectingKeyboard=false;phoneSubscribed=false;
    pairingUntil=phoneAt=keyboardAt=0;notification.clear();notificationOffset=0;notificationPhone=None;notificationAt=0;
    event(BleEventKind::Phone,"Phone disconnected",0);event(BleEventKind::Keyboard,"Keyboard disconnected",0);
    event(BleEventKind::Notice,"BLE host reset; reconnect devices");
}
int storeFull(ble_store_status_event*,void*){event(BleEventKind::Notice,"BLE bond store full; Forget a device");return BLE_HS_ESTORE_CAP;}
bool enable() {
    if(nimble_port_init()!=ESP_OK){event(BleEventKind::State,"BLE initialization failed",0);return false;}
    ble_hs_cfg.reset_cb=hostReset;ble_hs_cfg.sync_cb=hostSync;ble_hs_cfg.store_status_cb=storeFull;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_YESNO;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("Handheld OS");configureServices();
    if(ble_gatts_count_cfg(serviceDefinitions) || ble_gatts_add_svcs(serviceDefinitions)){nimble_port_deinit();event(BleEventKind::State,"BLE services could not start",0);return false;}
    ble_store_config_init();ble_npl_event_init(&workEvent,workCallback,nullptr);nimble_port_freertos_init(hostTask);return true;
}
void worker(void*) {
    uint32_t attempted=UINT32_MAX;bool shutdownFault=false;
    for(;;){BleRequest a{};const bool received=xQueueReceive(bleRequests,&a,pdMS_TO_TICKS(20))==pdTRUE;
        if(shutdownFault)continue;
        if(enabled && !gate.allowed()){
            dispatch(WorkKind::Shutdown);
            if(!nimble_port_stop() && nimble_port_deinit()==ESP_OK){
                enabled=false;hostActive=false;synced=false;phone=keyboard=None;connectingKeyboard=false;phoneSubscribed=false;hid={};pairs={};
                event(BleEventKind::State,gate.suspended()?"BLE off: Airplane mode":"BLE off",0);
            }else{
                // Never dispatch into a possibly stopped event queue or claim
                // RF is off after an ambiguous host/controller shutdown.
                shutdownFault=true;event(BleEventKind::Notice,"BLE shutdown failed; restart device");
            }
        }else if(!enabled && gate.allowed() && attempted!=gate.ticket()){
            hostActive=true;
            if(!gate.allowed()){hostActive=false;continue;}
            attempted=gate.ticket();enabled=enable();
            hostActive=enabled || esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED;
        }
        if(received && enabled && gate.accepts(a.ticket))dispatch(WorkKind::Request,&a);
        if(enabled && gate.allowed() && synced.load())dispatch(WorkKind::Poll);
    }
}
}
void bleSuspendForAirplane(bool suspended){gate.suspend(suspended);}
bool bleAirplaneStopped(){return !hostActive.load() && esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_ENABLED;}
bool startBleTask() {
    bleRequests=xQueueCreate(8,sizeof(BleRequest));bleEvents=xQueueCreate(48,sizeof(BleEvent));workComplete=xSemaphoreCreateBinary();
    if(!bleRequests || !bleEvents || !workComplete)return false;
    return xTaskCreatePinnedToCore(worker,"ble-setup",4096,nullptr,1,nullptr,0)==pdPASS;
}
bool bleRequest(BleCommand kind,const char* address,uint8_t addressType,uint32_t value,uint16_t handle,const char* text,uint8_t battery) {
    if(!bleRequests)return false;
    if(kind==BleCommand::Enable || kind==BleCommand::Disable){gate.prefer(kind==BleCommand::Enable);return true;}
    if(!gate.allowed())return false;
    BleRequest a{};a.kind=kind;a.ticket=gate.ticket();a.value=value;a.handle=handle;a.addressType=addressType;a.battery=battery;
    snprintf(a.address,sizeof(a.address),"%s",address?address:"");snprintf(a.text,sizeof(a.text),"%s",text?text:"");
    return xQueueSend(bleRequests,&a,0)==pdTRUE;
}
