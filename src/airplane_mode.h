// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
class DeviceSettings;
class WifiService;
class RadioService;
class GpsService;
// Initialize before any radio starts. Preferences survive suspension unchanged.
void initializeAirplaneMode(DeviceSettings&,WifiService&,RadioService&,GpsService&);
bool airplaneMode();
uint32_t airplaneGeneration();
// Always applies the requested state. False means the preference was not saved.
bool setAirplaneMode(bool enabled);
const char* airplaneStatus();
// Terminal, non-persistent shutdown: preserve all saved radio preferences.
void suspendRadiosForShutdown();
bool radiosStopped();
