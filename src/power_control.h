// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
bool startPowerControl();
void requestPowerOff();
bool powerOffRequested();
void powerOffReady();
size_t powerTaskStackFree();
