#pragma once

// Local settings take precedence without editing tracked defaults.
#if __has_include("local.h")
#include "local.h"
#endif

// User's board: 915 MHz, United States. No automatic transmissions at startup.
#ifndef DECK_LORA_MHZ
#define DECK_LORA_MHZ 915.0f
#endif
#ifndef DECK_LORA_BANDWIDTH
#define DECK_LORA_BANDWIDTH 125.0f
#endif
#ifndef DECK_LORA_SF
#define DECK_LORA_SF 7
#endif
#ifndef DECK_LORA_CR
#define DECK_LORA_CR 5
#endif
#ifndef DECK_LORA_SYNC
#define DECK_LORA_SYNC 0x12
#endif
#ifndef DECK_LORA_DBM
#define DECK_LORA_DBM 10
#endif
// -1 = detect using DRV2605 at 0x5A; 0 = v1.0; 1 = v1.1.
#ifndef DECK_BOARD_REVISION
#define DECK_BOARD_REVISION -1
#endif
#ifndef DECK_MODEM_BAUD
#define DECK_MODEM_BAUD 115200
#endif

static_assert(DECK_BOARD_REVISION == -1 || DECK_BOARD_REVISION == 0 || DECK_BOARD_REVISION == 1,
    "DECK_BOARD_REVISION must be -1 (auto), 0 (v1.0), or 1 (v1.1)");
