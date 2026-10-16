# Third-party code and hardware references

The project is distributed under GNU GPL version 3 (`GPL-3.0-only`); see [LICENSE](LICENSE). Upstream copyright and license notices are retained in vendored source and the accompanying license files. Third-party components keep their own licenses.

## Vendored e-paper driver

`lib/GxEPD2/` is a minimal **GxEPD2 1.5.5 / GDEQ031T10** subset from LILYGO's T-Deck Pro repository at commit [`53bcfa593b1df064894a8efc5a7e15e4524b09d2`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/53bcfa593b1df064894a8efc5a7e15e4524b09d2/lib/GxEPD2). It includes the generic SPI/e-paper base, black-and-white wrapper and this panel's driver. Unused display drivers/examples were omitted. The upstream project is [ZinggJM/GxEPD2](https://github.com/ZinggJM/GxEPD2), authored by Jean-Marc Zingg, with panel support carried in LILYGO's reference tree.

The included [GxEPD2 license](lib/GxEPD2/LICENSE) is GPL version 3. Source notices remain in the files. `library.json` is a small local manifest identifying the retained subset and its Adafruit GFX dependency. The project license was chosen to remain compatible with this GPL driver.

## Firmware build dependencies

Direct dependencies are pinned in `platformio.ini`:

| Component | Version | License / source |
| --- | --- | --- |
| PlatformIO Espressif32 platform | 6.10.0 | Apache-2.0; [platform-espressif32](https://github.com/platformio/platform-espressif32/tree/v6.10.0) |
| Arduino-ESP32 framework | 2.0.17, provided by that platform | LGPL-2.1-or-later; [arduino-esp32](https://github.com/espressif/arduino-esp32/tree/2.0.17) |
| Adafruit GFX Library | 1.11.11 | BSD; [source](https://github.com/adafruit/Adafruit-GFX-Library/tree/1.11.11), [license](docs/licenses/Adafruit-GFX.txt) |
| Adafruit BusIO | 1.16.2 | MIT; [source](https://github.com/adafruit/Adafruit_BusIO/tree/1.16.2), [license](docs/licenses/Adafruit-BusIO.txt) |
| Adafruit TCA8418 | 1.0.2 | BSD-3-Clause; [source](https://github.com/adafruit/Adafruit_TCA8418/tree/1.0.2), [license](docs/licenses/Adafruit-TCA8418.txt) |
| RadioLib | 7.1.2 | MIT; [source](https://github.com/jgromes/RadioLib/tree/7.1.2), [license](docs/licenses/RadioLib.txt) |

The framework includes Espressif/ESP-IDF, FreeRTOS and other upstream components under their respective licenses. PlatformIO downloads the toolchain/framework packages separately; retain their notices when redistributing those packages. The repository is not a vendored copy of the complete SDK or compiler.

Developer tools are pinned in `requirements-dev.txt`: PlatformIO 6.1.19 (Apache-2.0) and pyserial 3.5 (BSD). Flashing instructions use esptool 4.5.1 (GPL-2.0-or-later) as a host utility. They are separate host-side programs, not code copied into the application.

## Hardware and behavior references

- [LILYGO T-Deck Pro](https://github.com/Xinyuan-LilyGO/T-Deck-Pro), reference commit `53bcfa593b1df064894a8efc5a7e15e4524b09d2`: board pin definitions, modem startup examples, display and keyboard hardware facts.
- [LILYGO v1.1 / HD-V2-250915](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/02b3e82a64926a1d9f907f3b5538a40d1e48795c): v1.1 reset wiring and revision-specific peripherals.
- [Meshtastic TDeckProKeyboard.cpp](https://github.com/meshtastic/firmware/blob/master/src/input/TDeckProKeyboard.cpp): cross-check of physical keyboard event numbers, Space/Sym and modifiers. No complete Meshtastic implementation was copied, and the starter does not implement the Meshtastic protocol.
- [SIMCom A7682E](https://en.simcom.com/product/A7682E.html): specified RF bands and modem capabilities.
- [T-Mobile US network guidance](https://www.t-mobile.com/support/coverage/t-mobile-network): carrier spectrum context. A published band list is not certification of this device, its firmware or this SIM's voice service.

The firmware does not redistribute the original factory application, modem firmware, user SIM contents or factory flash dump.
