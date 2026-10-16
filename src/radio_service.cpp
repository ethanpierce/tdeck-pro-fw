#include "radio_service.h"
#include "board.h"
#include "settings.h"
#include "mesh_crypto.h"
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "platform.h"
#include <algorithm>
#include <cstring>
namespace {
class NativeRadioHal final:public RadioLibHal {
public:
    NativeRadioHal():RadioLibHal(GPIO_MODE_INPUT,GPIO_MODE_OUTPUT,0,1,GPIO_INTR_POSEDGE,GPIO_INTR_NEGEDGE){}
    // Module::init calls init(), not spiBegin(). The generic RadioLib HAL's
    // default init is empty; explicitly attach to the already initialized bus.
    void init() override {spiBegin();}
    void term() override {spiEnd();}
    void pinMode(uint32_t pin,uint32_t mode) override {
        if(pin==RADIOLIB_NC)return;
        if(pin>=49){failed_=true;return;}
        gpio_config_t config{};config.pin_bit_mask=uint64_t(1)<<pin;config.mode=gpio_mode_t(mode);
        // gpio_config selects PIN_FUNC_GPIO as well as direction. Merely
        // changing direction leaves a pad connected to its reset peripheral.
        if(gpio_config(&config)!=ESP_OK)failed_=true;
    }
    void digitalWrite(uint32_t pin,uint32_t value) override {if(pin!=RADIOLIB_NC)gpio_set_level(gpio_num_t(pin),value);}
    uint32_t digitalRead(uint32_t pin) override {return pin==RADIOLIB_NC?0:uint32_t(gpio_get_level(gpio_num_t(pin)));}
    void attachInterrupt(uint32_t pin,void(*callback)(),uint32_t mode) override {
        if(pin>=49)return;
        const auto result=gpio_install_isr_service(0);
        if(result!=ESP_OK && result!=ESP_ERR_INVALID_STATE)return;
        callbacks_[pin]=callback;gpio_set_intr_type(gpio_num_t(pin),gpio_int_type_t(mode));
        gpio_isr_handler_add(gpio_num_t(pin),[](void* arg){const auto n=uintptr_t(arg);if(callbacks_[n])callbacks_[n]();},reinterpret_cast<void*>(uintptr_t(pin)));
    }
    void detachInterrupt(uint32_t pin) override {if(pin<49){gpio_isr_handler_remove(gpio_num_t(pin));callbacks_[pin]=nullptr;}}
    void delay(RadioLibTime_t ms) override {platform::delay(uint32_t(ms));}
    void delayMicroseconds(RadioLibTime_t us) override {esp_rom_delay_us(uint32_t(us));}
    RadioLibTime_t millis() override {return platform::millis();}
    RadioLibTime_t micros() override {return RadioLibTime_t(esp_timer_get_time());}
    long pulseIn(uint32_t pin,uint32_t state,RadioLibTime_t timeout) override {
        if(pin==RADIOLIB_NC)return 0;
        const auto start=micros();
        while(digitalRead(pin)==state){if(micros()-start>=timeout)return 0;taskYIELD();}
        while(digitalRead(pin)!=state){if(micros()-start>=timeout)return 0;taskYIELD();}
        const auto edge=micros();
        while(digitalRead(pin)==state){if(micros()-start>=timeout)return 0;taskYIELD();}
        return long(micros()-edge);
    }
    void spiBegin() override {if(!device_)device_=platform::spiDevice(-1,4000000,0);failed_=!device_;}
    void spiBeginTransaction() override {acquired_=device_ && platform::spiAcquire(device_);if(!acquired_)failed_=true;}
    void spiTransfer(uint8_t* tx,size_t size,uint8_t* rx) override {
        if(!acquired_ || !platform::spiTransfer(device_,tx,rx,size)){failed_=true;if(rx)std::memset(rx,0xff,size);}
    }
    void spiEndTransaction() override {if(acquired_)platform::spiRelease(device_);acquired_=false;}
    // The shared SPI bus/device live for the application lifetime. Never tear
    // down the display's bus when a RadioLib object terminates.
    void spiEnd() override {}
    void yield() override {taskYIELD();}
    bool failed()const{return failed_;}
private:
    spi_device_handle_t device_=nullptr;bool acquired_=false,failed_=false;
    static inline void(*callbacks_[49])()={};
};
NativeRadioHal& nativeHal(){static NativeRadioHal hal;return hal;}
}

RadioService::RadioService():radio_(new Module(&nativeHal(),board::RadioCs,board::RadioIrq,board::RadioReset,board::RadioBusy)){}
void RadioService::report(const char* text){if(status_==text)return;status_=text;if(changed)changed();}
void RadioService::powerDown() {
    const bool interrupted=queued_ || transmitting_ || cad_;
    const uint32_t id=current_;
    // Remove RF power immediately, then hold reset asserted. No SPI commands
    // or reinitialization are attempted against the powered-down radio.
    if(!board::unsupported){
        platform::gpioOutput(board::RadioPower,false);
        platform::gpioOutput(board::RadioReset,false);
        platform::gpioOutput(board::RadioCs,true);
    }
    ready=initialized_=transmitting_=queued_=cad_=false;
    txBytes_={};
    if(interrupted && sent)sent(id,false);
}
bool RadioService::configure(const deck::MeshOptions& options) {
    if(!deck::validMeshOptions(options))return false;
    powerDown();options_=options;gate_.prefer(options.enabled!=0);
    duplicates_.clear();nodes_={};
    begin();if(changed)changed();return true;
}
void RadioService::suspendForAirplane(bool suspended) {
    if(gate_.suspended()==suspended)return;
    gate_.suspend(suspended);
    if(suspended){powerDown();report("Airplane mode");}
    else begin();
}
bool RadioService::begin() {
    if(board::unsupported)return false;
    if(!node){
        uint8_t mac[6]{};esp_efuse_mac_get_default(mac);
        node=(uint32_t(mac[2])<<24)|(uint32_t(mac[3])<<16)|(uint32_t(mac[4])<<8)|mac[5];
        if(!node || node==deck::MeshBroadcast)node=1;
        sequence_=esp_random();
        cryptoReady_=meshCryptoSelfTest();
    }
    if(!gate_.allowed() || !configured() || !cryptoReady_) {
        powerDown();
        report(gate_.suspended()?"Airplane mode":!options_.enabled?"Mesh off":!configured()?"Select US region":"Mesh crypto self-test failed");
        return false;
    }
    ready=initialized_=transmitting_=queued_=cad_=false;
    platform::gpioOutput(board::RadioCs,true);platform::gpioOutput(board::RadioReset,false);
    platform::gpioOutput(board::RadioPower,true);platform::delay(10);
    error=radio_.begin(deck::meshFrequencyMhz(options_),deck::meshBandwidthKhz(options_.preset),deck::meshSpreadingFactor(options_.preset),5,0x2b,DECK_LORA_DBM,16,2.4);
    if(nativeHal().failed())error=RADIOLIB_ERR_SPI_WRITE_FAILED;
    if(error==RADIOLIB_ERR_NONE)error=radio_.setDio2AsRfSwitch(true);
    if(error==RADIOLIB_ERR_NONE)error=radio_.setCRC(2);
    if(error!=RADIOLIB_ERR_NONE){powerDown();report("Mesh radio unavailable");return false;}
    initialized_=true;listen();return ready;
}
void RadioService::listen(int previousError) {
    if(!gate_.allowed() || !configured() || !initialized_){ready=false;return;}
    retryAt_=platform::millis();
    const int state=radio_.startReceive();ready=state==RADIOLIB_ERR_NONE;
    error=ready?previousError:state;
    report(ready?"Mesh ready":"Mesh receive unavailable");
}
bool RadioService::queueData(uint32_t port,const uint8_t* bytes,size_t size,uint32_t id) {
    if(!gate_.allowed() || !configured() || !ready || transmitting_ || queued_ || cad_)return false;
    deck::MeshBuffer data;
    if(!deck::meshEncodeData(port,bytes,size,false,data))return false;
    if(++sequence_==0)++sequence_;
    deck::MeshHeader header;header.from=node;header.id=sequence_;
    header.hopLimit=header.hopStart=options_.hopLimit;header.channel=deck::meshChannelHash(options_);header.relay=uint8_t(node);
    txBytes_={};
    if(!deck::meshEncodeHeader(header,txBytes_.bytes.data(),txBytes_.bytes.size()) || !meshCrypt(options_,node,sequence_,data.bytes.data(),data.size))return false;
    std::memcpy(txBytes_.bytes.data()+deck::MeshHeaderSize,data.bytes.data(),data.size);txBytes_.size=data.size+deck::MeshHeaderSize;
    current_=id;queued_=true;cadAttempts_=0;queuedAt_=platform::millis();sendAt_=queuedAt_+100+(esp_random()%300);
    report("Mesh message queued");return true;
}
bool RadioService::send(const std::string& text,uint32_t id) {
    if(text.empty() || text.size()>deck::MeshSendTextLimit)return false;
    deck::MeshData data;data.port=deck::MeshTextPort;data.size=text.size();std::memcpy(data.payload.data(),text.data(),text.size());
    char checked[deck::MeshTextLimit+1];if(!deck::meshText(data,checked,sizeof(checked)))return false;
    return queueData(deck::MeshTextPort,reinterpret_cast<const uint8_t*>(text.data()),text.size(),id);
}
bool RadioService::announce() {
    deck::MeshBuffer user;
    return deck::meshEncodeUser(node,options_,user) && queueData(deck::MeshNodeInfoPort,user.bytes.data(),user.size,0);
}
void RadioService::complete(bool success,int result) {
    const uint32_t id=current_;transmitting_=queued_=cad_=false;txBytes_={};
    listen(result);report(success?"Mesh transmitted; receipt unconfirmed":"Mesh send did not complete");
    if(sent)sent(id,success);
}
void RadioService::heard(uint32_t from,int rssi,const deck::MeshUser* user) {
    auto* slot=&nodes_[0];const auto now=platform::millis();
    for(auto& entry:nodes_){
        if(entry.id==from || !entry.id){slot=&entry;break;}
        if(uint32_t(now-entry.lastSeen)>uint32_t(now-slot->lastSeen))slot=&entry;
    }
    if(slot->id!=from)*slot={};
    slot->id=from;slot->lastSeen=now;slot->rssi=int16_t(rssi);
    if(user)slot->user=*user;
    if(changed)changed();
}
void RadioService::receivePacket(const uint8_t* bytes,size_t size,int rssi) {
    deck::MeshHeader header;
    if(!deck::meshDecodeHeader(bytes,size,header) || header.from==node || header.to!=deck::MeshBroadcast || header.channel!=deck::meshChannelHash(options_))return;
    deck::MeshBuffer plain;plain.size=size-deck::MeshHeaderSize;std::memcpy(plain.bytes.data(),bytes+deck::MeshHeaderSize,plain.size);
    if(!meshCrypt(options_,header.from,header.id,plain.bytes.data(),plain.size))return;
    deck::MeshData data;if(!deck::meshDecodeData(plain.bytes.data(),plain.size,data))return;
    if(data.port==deck::MeshTextPort){
        char text[deck::MeshTextLimit+1];if(!deck::meshText(data,text,sizeof(text)) || !duplicates_.remember(header.from,header.id))return;
        heard(header.from,rssi);
        if(received)received({header.from,header.id,text},rssi);
    } else if(data.port==deck::MeshNodeInfoPort){
        deck::MeshUser user;if(!deck::meshDecodeUser(data.payload.data(),data.size,user) || !duplicates_.remember(header.from,header.id))return;
        heard(header.from,rssi,&user);
    }
}
void RadioService::poll() {
    if(!gate_.allowed() || !configured())return;
    const auto now=platform::millis();
    if(!ready){if(initialized_ && uint32_t(now-retryAt_)>=1000)listen(error);return;}
    const bool irq=platform::gpioRead(board::RadioIrq)!=0;
    if(cad_){
        if(!irq && uint32_t(now-started_)<1000)return;
        const uint32_t flags=radio_.getIrqFlags();
        if(!(flags&RADIOLIB_SX126X_IRQ_CAD_DONE)){complete(false,RADIOLIB_ERR_RX_TIMEOUT);return;}
        cad_=false;
        if(flags&RADIOLIB_SX126X_IRQ_CAD_DETECTED){
            if(++cadAttempts_>=4 || uint32_t(now-queuedAt_)>=5000){complete(false,RADIOLIB_LORA_DETECTED);return;}
            listen();sendAt_=now+200+(esp_random()%600);report("Mesh waiting for clear channel");return;
        }
        error=radio_.standby();
        if(error==RADIOLIB_ERR_NONE)error=radio_.startTransmit(txBytes_.bytes.data(),txBytes_.size);
        if(error!=RADIOLIB_ERR_NONE){complete(false,error);return;}
        queued_=false;transmitting_=true;started_=now;
        txTimeout_=1000U+2U*((radio_.getTimeOnAir(txBytes_.size)+999U)/1000U);report("Mesh transmitting");return;
    }
    if(transmitting_){
        const bool timeout=uint32_t(now-started_)>=txTimeout_;
        if(!irq && !timeout)return;
        const uint32_t flags=radio_.getIrqFlags();
        const bool done=(flags&RADIOLIB_SX126X_IRQ_TX_DONE) && !(flags&RADIOLIB_SX126X_IRQ_TIMEOUT);
        if(!done && !timeout && !(flags&RADIOLIB_SX126X_IRQ_TIMEOUT))return;
        int result=done?RADIOLIB_ERR_NONE:RADIOLIB_ERR_TX_TIMEOUT;
        const int finish=radio_.finishTransmit();if(result==RADIOLIB_ERR_NONE)result=finish;
        complete(result==RADIOLIB_ERR_NONE,result);return;
    }
    if(irq){
        const uint32_t flags=radio_.getIrqFlags();
        if(flags&(RADIOLIB_SX126X_IRQ_RX_DONE|RADIOLIB_SX126X_IRQ_TIMEOUT|RADIOLIB_SX126X_IRQ_CRC_ERR|RADIOLIB_SX126X_IRQ_HEADER_ERR)){
            int result=radio_.standby();uint8_t bytes[deck::MeshFrameLimit];size_t size=0;int rssi=0;
            if(result==RADIOLIB_ERR_NONE){
                if(flags&RADIOLIB_SX126X_IRQ_TIMEOUT)result=RADIOLIB_ERR_RX_TIMEOUT;
                else {size=radio_.getPacketLength();if(size && size<=sizeof(bytes)){result=radio_.readData(bytes,size);if(result==RADIOLIB_ERR_NONE)rssi=int(radio_.getRSSI());}else result=RADIOLIB_ERR_PACKET_TOO_LONG;}
            }
            listen(result);if(result==RADIOLIB_ERR_NONE)receivePacket(bytes,size,rssi);
        }
    }
    if(queued_ && !cad_ && !transmitting_ && int32_t(now-sendAt_)>=0){
        error=radio_.startChannelScan();if(error!=RADIOLIB_ERR_NONE){complete(false,error);return;}
        cad_=true;started_=now;report("Mesh checking channel");
    }
}
