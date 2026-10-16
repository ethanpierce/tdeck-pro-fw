#pragma once
#include <cstdint>
namespace board {
constexpr int Sda=13, Scl=14, Sck=36, Mosi=33, Miso=47;
constexpr int EpdCs=34, EpdDc=35, EpdBusy=37, SdCs=48;
constexpr int KeyboardLight=42, FrontLight=45, GpsPower=39;
constexpr int GpsEspRx=44, GpsEspTx=43;
constexpr int RadioCs=3, RadioIrq=5, RadioReset=4, RadioBusy=6, RadioPower=46;
// LILYGO names TXD/RXD from the MODEM's perspective.
constexpr int ModemEspRx=11, ModemEspTx=10, ModemPower=41, ModemKey=40, ModemReset=9, ModemDtr=8;
class Screen;
extern Screen* screen;
extern bool revision11, unsupported;
bool probe(uint8_t address);
void begin();
int batteryPercent();
void lights(bool front,bool keyboard);
// Cellular task only: prepare the rail, then pulse the inverted PWRKEY.
bool startModemPower(bool (*allowed)()=nullptr);
void stopModemPower();
}
