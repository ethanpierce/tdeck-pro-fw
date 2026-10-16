# Hardware and cellular constraints

## Target and verified references

The user's photographs identify a **T-Deck Pro / A7682E**. The user confirmed **United States**, **T-Mobile SIM**, and **915 MHz LoRa**. The enclosure photos do not establish the PCB revision or prove LTE/voice service.

Hardware definitions were checked against LILYGO's repository:

- [T-Deck Pro reference at `53bcfa593b1df064894a8efc5a7e15e4524b09d2`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/53bcfa593b1df064894a8efc5a7e15e4524b09d2), including the v1.0 pin map, modem examples, keyboard and display driver.
- [v1.1 / HD-V2-250915 reference at `02b3e82a64926a1d9f907f3b5538a40d1e48795c`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/02b3e82a64926a1d9f907f3b5538a40d1e48795c).
- [Vendor revision guide](https://github.com/Xinyuan-LilyGO/T-Deck-Pro#zero-version-) and [download instructions](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/master/firmware).

The starter detects a DRV2605 at I²C `0x5A` as a v1.1 indication. An expander at `0x20` indicates the unsupported MAX profile and disables this starter's radios. This is a peripheral-based heuristic, so the local session must compare it against the actual PCB. `DECK_BOARD_REVISION` can be overridden to `0` for v1.0 or `1` for v1.1 if needed; `-1` is automatic.

## Board interfaces used

| Function | ESP32-S3 GPIO / setting |
| --- | --- |
| I²C | SDA 13, SCL 14 |
| Shared SPI | SCK 36, MOSI 33, MISO 47 |
| E-paper GDEQ031T10, 240 × 320 | CS 34, DC 35, busy 37; reset unconnected on v1.0, GPIO 16 on v1.1 |
| SX1262 | CS 3, reset 4, DIO1 5, busy 6, power enable 46 |
| Modem UART | **ESP RX 11 ← modem TX; ESP TX 10 → modem RX**, 115200, 8N1 |
| Modem controls | Power enable 41, PWRKEY 40, reset 9, DTR 8 |
| SD chip select | 48, held inactive; no SD filesystem in this starter |
| Keyboard | TCA8418 over I²C; scan events decoded in `lib/DeckCore/src/keyboard_decoder.cpp` |
| Battery gauge | BQ27220 at `0x55`, state-of-charge register `0x2C` read only |
| Keyboard backlight | 42, off |
| E-paper front light | 45 on v1.1, off |
| GPS power | 39, off |

LILYGO's `BOARD_A7682E_TXD` / `RXD` labels are from the **modem's perspective**. Reversing this distinction is a common cause of a modem that never answers AT commands.

The board uses a transistor on PWRKEY. The starter follows the vendor's GPIO-high pulse rather than treating it as a directly wired active-low modem pin. Keep the battery connected for modem tests; USB enumeration does not prove the modem has adequate power for registration and transmit bursts.

On v1.1, GPIO 45 controls the e-paper front light and is held low to keep it off. Touch reset is GPIO 38 on v1.1 and GPIO 45 on v1.0; no touch driver is implemented. The starter reads the factory battery gauge without recalibration and does not implement charger configuration or an optimized sleep strategy.

## Why the T-Mobile SIM may not work

[SIMCom's A7682E specification](https://en.simcom.com/product/A7682E.html) lists **LTE-FDD B1/B3/B5/B7/B8/B20** and GSM 900/1800 MHz. [T-Mobile's US network guidance](https://www.t-mobile.com/support/coverage/t-mobile-network) lists its deployed spectrum, including LTE B4/B5/B12/B66/B71; B2 is also commonly associated with T-Mobile US LTE. The A7682E lacks B2/B4/B12/B66/B71. B5 is the limited possible LTE overlap, not a basis for broad US coverage.

The modem's 900/1800 MHz GSM fallback does not solve US compatibility. It cannot receive bands absent from its RF hardware. An APN primarily configures packet-data service after the RF/network path is available; changing it does not repair missing bands, antenna wiring, an unrecognized SIM or carrier device eligibility. This starter does not start a packet-data connection or assume an APN is the explanation for failed registration.

Check the stages separately:

1. `AT` response proves the ESP32 can communicate with the modem.
2. `CPIN: READY` proves the SIM is readable and unlocked.
3. `CEREG` status `1` or `5` indicates normal home/roaming LTE registration; `6`/`7` indicates SMS-only registration, `9`/`10` registration with CSFB not preferred, and `8`/`11` emergency-only service. Service scope still needs testing.
4. Successful SMS in both directions proves that supported SMS works with the SIM/carrier configuration.
5. Incoming/outgoing calls with audio prove voice service and audio routing. LTE registration alone is insufficient.

The firmware emits the model, modem firmware revision, function mode, SIM state, registration, signal, operator and serving-system responses through USB diagnostics. Keep these observations for the next implementation decision. Do not substitute guesses about cellular coverage for device results.

## Calls and audio

The starter issues `ATD<number>;`, `ATA`, `AT+CHUP`, enables caller-number reporting and polls `AT+CLCC`. `AT+CHUP` avoids the modem's `ATH` behavior depending on `CVHU`. These are call controls. The modem's supported firmware, carrier provisioning/VoLTE/IMS acceptance and the board's microphone/speaker path determine whether usable voice is possible.

No audio interface or routing setup has been validated. No claim is made that the current starter produces audible calls. If the modem's particular firmware lacks the required carrier voice support or the carrier rejects it, UI work cannot solve that requirement.

## LoRa

The SX1262 path is independent of the SIM. The 915 MHz board can exchange messages with a second compatible peer using identical physical-layer settings and the [Handheld OS packet format](PROTOCOL.md). The default power is 10 dBm, with no application payload transmitted automatically at boot. Fit the correct antenna before intentional transmissions and keep settings within the device's intended regional use.

The current implementation is a small direct-radio test/chat service. It is not a Meshtastic client, LoRaWAN device or routed mesh. Choose a mature interoperable stack later if interoperability, encryption, acknowledgements or multi-hop messaging are required.
