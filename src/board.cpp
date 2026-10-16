#include "board.h"
#include "settings.h"
#include "native_screen.h"
#include <platform.h>
#include <initializer_list>
#include <new>
#include <driver/gpio.h>
namespace board {
Screen* screen = nullptr;
bool revision11 = false, unsupported = false;
namespace {
void output(int pin, int level) {
    // Set the latch before enabling the output to avoid an active-low glitch.
    platform::gpioOutput(pin,level!=0);
}
}
bool probe(uint8_t address) {
    // Match LILYGO's revision probe; a first I2C transaction can fail at boot.
    for (unsigned attempt=0; attempt<3; ++attempt) {
        if(platform::i2cProbe(address))return true;
        platform::delay(10);
    }
    return false;
}
void begin() {
    if(!platform::i2cBegin(Sda,Scl))return;
    unsupported = probe(0x20); // MAX uses an expander and a different board profile.
    if (unsupported) return; // Leave other profiles' pins untouched.
    revision11 = DECK_BOARD_REVISION < 0 ? probe(0x5a) : DECK_BOARD_REVISION == 1;

    // The factory firmware can leave these outputs held during deep sleep.
    // Release modem controls and shared-SPI selects/reset as well, before
    // their output levels are configured below or by the cellular task.
    for (int pin : {ModemPower, RadioPower, GpsPower, ModemKey, ModemReset, ModemDtr,
                    EpdCs, SdCs, RadioCs, RadioReset}) {
        gpio_hold_dis(static_cast<gpio_num_t>(pin));
    }
    gpio_deep_sleep_hold_dis();
    for (int pin : {EpdCs, SdCs, RadioCs, RadioReset}) output(pin, true);
    output(KeyboardLight, false);
    if (revision11) output(FrontLight, false);
    output(GpsPower, false); // The Location preference and Airplane gate own GNSS startup.
    output(RadioPower,false);
    stopModemPower(); // Saved Airplane mode is applied before either rail starts.
    if(!platform::spiBegin(Sck,Miso,Mosi))return;
    screen=new(std::nothrow) Screen(EpdCs,EpdDc,revision11?16:-1,EpdBusy);
    if(screen && !screen->begin()){delete screen;screen=nullptr;}
}
int batteryPercent() {
    // Read the BQ27220 only. Never change the factory gauge calibration.
    const uint8_t reg=0x2c;uint8_t bytes[2]{};
    if(!platform::i2cWriteRead(0x55,&reg,1,bytes,2))return -1;
    const int percent=bytes[0]|(int(bytes[1])<<8);
    return percent <= 100 ? percent : -1;
}
void lights(bool front,bool keyboard) {
    if(unsupported) return;
    gpio_hold_dis(static_cast<gpio_num_t>(KeyboardLight));
    output(KeyboardLight,keyboard?true:false);
    // GPIO45 is touch reset on v1.0; never drive it as a light there.
    if(revision11) {
        gpio_hold_dis(static_cast<gpio_num_t>(FrontLight));
        output(FrontLight,front?true:false);
    }
}
void stopModemPower() {
    if(unsupported)return;
    output(ModemPower,false);
    output(ModemKey,false);
    output(ModemDtr,false);
    output(ModemReset,true);
}
bool startModemPower(bool (*allowed)()) {
    if (unsupported) return false;
    auto permitted=[allowed](){return !allowed || allowed();};
    auto wait=[&](uint32_t ms){
        for(uint32_t elapsed=0;elapsed<ms;elapsed+=10) {
            if(!permitted()){stopModemPower();return false;}
            platform::delay(10);
        }
        if(!permitted()){stopModemPower();return false;}
        return true;
    };
    if(!permitted()){stopModemPower();return false;}
    // Hold reset inactive and the key released before enabling the supply.
    // A hardware reset pulse is deliberately avoided; LILYGO warns that it
    // can change the module's power-off behavior on its first power-up.
    output(ModemKey, false);
    output(ModemReset, true);
    output(ModemDtr, false); // Keep the modem UART awake.
    output(ModemPower, true);
    if(!wait(2000))return false;
    // Board transistor inverts PWRKEY; vendor uses a 50 ms HIGH pulse.
    platform::gpioWrite(ModemKey,true);
    if(!wait(50))return false;
    platform::gpioWrite(ModemKey,false);
    return wait(3000);
}
}
