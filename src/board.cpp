#include "board.h"
#include "settings.h"
#include <Wire.h>
#include <SPI.h>
#include <driver/gpio.h>
namespace board {
Screen* screen = nullptr;
bool revision11 = false, unsupported = false;
namespace {
void output(int pin, int level) {
    // Set the latch before enabling the output to avoid an active-low glitch.
    digitalWrite(pin, level);
    pinMode(pin, OUTPUT);
}
}
bool probe(uint8_t address) {
    // Match LILYGO's revision probe; a first I2C transaction can fail at boot.
    for (unsigned attempt=0; attempt<3; ++attempt) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) return true;
        delay(10);
    }
    return false;
}
void begin() {
    Wire.begin(Sda, Scl);
    Wire.setTimeOut(30);
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
    for (int pin : {EpdCs, SdCs, RadioCs, RadioReset}) output(pin, HIGH);
    output(KeyboardLight, LOW);
    if (revision11) output(FrontLight, LOW);
    output(GpsPower, LOW); // GPS off for this milestone.
    SPI.begin(Sck, Miso, Mosi);
    screen = new Screen(GxEPD2_310_GDEQ031T10(EpdCs, EpdDc, revision11 ? 16 : -1, EpdBusy));
    screen->epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    screen->init(0, true, 2, false);
    screen->setRotation(0);
    screen->setTextColor(GxEPD_BLACK);
    screen->setTextWrap(false);
}
int batteryPercent() {
    // Read the BQ27220 only. Never change the factory gauge calibration.
    Wire.beginTransmission(0x55);
    Wire.write(0x2c);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(uint8_t(0x55), uint8_t(2)) != 2) return -1;
    int lo=Wire.read(), hi=Wire.read();
    int percent=lo | (hi << 8);
    return percent <= 100 ? percent : -1;
}
void lights(bool front,bool keyboard) {
    if(unsupported) return;
    gpio_hold_dis(static_cast<gpio_num_t>(KeyboardLight));
    output(KeyboardLight,keyboard?HIGH:LOW);
    // GPIO45 is touch reset on v1.0; never drive it as a light there.
    if(revision11) {
        gpio_hold_dis(static_cast<gpio_num_t>(FrontLight));
        output(FrontLight,front?HIGH:LOW);
    }
}
void startModemPower() {
    if (unsupported) return;
    // Hold reset inactive and the key released before enabling the supply.
    // A hardware reset pulse is deliberately avoided; LILYGO warns that it
    // can change the module's power-off behavior on its first power-up.
    output(ModemKey, LOW);
    output(ModemReset, HIGH);
    output(ModemDtr, LOW); // Keep the modem UART awake.
    output(ModemPower, HIGH);
    delay(2000);
    // Board transistor inverts PWRKEY; vendor uses a 50 ms HIGH pulse.
    digitalWrite(ModemKey, HIGH); delay(50); digitalWrite(ModemKey, LOW);
    delay(3000);
}
}
