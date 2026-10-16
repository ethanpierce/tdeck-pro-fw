// SPDX-License-Identifier: GPL-3.0-only
#include "airplane_mode.h"
#include "device_settings.h"
#include "wifi_service.h"
#include "ble_service.h"
#include "modem_task.h"
#include "radio_service.h"
#include "gps_service.h"
#include <atomic>
namespace {
// Fail closed until settings are loaded and every service has its gate applied.
std::atomic<bool> enabled{true};
std::atomic<uint32_t> generation{0};
DeviceSettings* preferences=nullptr;
WifiService* wifi=nullptr;
RadioService* mesh=nullptr;
GpsService* gps=nullptr;
bool saved=true;
void apply(bool value) {
    if(value)generation.fetch_add(1);
    enabled=value;
    cellSuspendForAirplane(value);
    bleSuspendForAirplane(value);
    if(wifi)wifi->suspendForAirplane(value);
    if(mesh)mesh->suspendForAirplane(value);
    if(gps)gps->suspendForAirplane(value);
}
}
bool airplaneMode(){return enabled.load();}
uint32_t airplaneGeneration(){return generation.load();}
void initializeAirplaneMode(DeviceSettings& settings,WifiService& wireless,RadioService& radio,GpsService& location) {
    preferences=&settings;wifi=&wireless;mesh=&radio;gps=&location;
    saved=true;
    apply(settings.radios().airplane!=0);
}
bool setAirplaneMode(bool value) {
    if(!preferences)return false;
    apply(value); // Power down even if NVS cannot save the preference.
    auto options=preferences->radios();options.airplane=value;
    saved=preferences->saveRadios(options);
    return saved;
}
const char* airplaneStatus() {
    if(!enabled.load())return saved?"Off":"Off. Setting was not saved.";
    if(!cellAirplaneStopped() || !bleAirplaneStopped() || (wifi && !wifi->airplaneStopped()) ||
       (mesh && mesh->ready) || (gps && gps->snapshot().enabled))return "Turning off radios...";
    return saved?"On. All radios are off.":"On. Radios off; setting was not saved.";
}
