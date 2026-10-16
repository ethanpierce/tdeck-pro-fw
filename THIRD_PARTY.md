# Third-party code and hardware references

The project is distributed under GNU GPL version 3 (`GPL-3.0-only`); see [LICENSE](LICENSE). Upstream copyright and license notices are retained in vendored source and the accompanying license files. Third-party components keep their own licenses.

For the v0.1.0 binary release, the corresponding project source is [tag v0.1.0](https://github.com/ethanpierce/tdeck-pro-fw/tree/v0.1.0) or the release asset [Handheld-OS-v0.1.0-source.zip](https://github.com/ethanpierce/tdeck-pro-fw/releases/download/v0.1.0/Handheld-OS-v0.1.0-source.zip). The source contains the build scripts, board definition, configuration and exact PlatformIO dependency pins. `BUILD_INFO.json` in each firmware package records the source commit and image hashes. The dependency sources below are available separately at their pinned upstream versions.

## Vendored e-paper driver

`lib/GxEPD2/` is a minimal **GxEPD2 1.5.5 / GDEQ031T10** subset from LILYGO's T-Deck Pro repository at commit [`53bcfa593b1df064894a8efc5a7e15e4524b09d2`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/53bcfa593b1df064894a8efc5a7e15e4524b09d2/lib/GxEPD2). It includes the generic SPI/e-paper base, black-and-white wrapper and this panel's driver. Unused display drivers/examples were omitted. The upstream project is [ZinggJM/GxEPD2](https://github.com/ZinggJM/GxEPD2), authored by Jean-Marc Zingg, with panel support carried in LILYGO's reference tree.

The included [GxEPD2 license](lib/GxEPD2/LICENSE) is GPL version 3. Source notices remain in the files. `library.json` is a small local manifest identifying the retained subset and its Adafruit GFX dependency. The project license was chosen to remain compatible with this GPL driver.

## Firmware build dependencies

Direct dependencies are pinned in `platformio.ini`:

| Component | Version | License / source |
| --- | --- | --- |
| PlatformIO Espressif32 platform | 6.10.0 | Apache-2.0; [platform-espressif32](https://github.com/platformio/platform-espressif32/tree/v6.10.0) |
| Arduino-ESP32 framework | 2.0.17, provided by that platform | LGPL-2.1-or-later; [source](https://github.com/espressif/arduino-esp32/tree/2.0.17), [license](docs/licenses/Arduino-ESP32-LGPL-2.1.md) |
| Adafruit GFX Library | 1.11.11 | BSD; [source](https://github.com/adafruit/Adafruit-GFX-Library/tree/1.11.11), [license](docs/licenses/Adafruit-GFX.txt) |
| Adafruit BusIO | 1.16.2 | MIT; [source](https://github.com/adafruit/Adafruit_BusIO/tree/1.16.2), [license](docs/licenses/Adafruit-BusIO.txt) |
| Adafruit TCA8418 | 1.0.2 | BSD-3-Clause; [source](https://github.com/adafruit/Adafruit_TCA8418/tree/1.0.2), [license](docs/licenses/Adafruit-TCA8418.txt) |
| RadioLib | 7.1.2 | MIT; [source](https://github.com/jgromes/RadioLib/tree/7.1.2), [license](docs/licenses/RadioLib.txt) |
| NimBLE-Arduino | 2.5.1 | Apache-2.0; Ryan Powell / h2zero and contributors; [source](https://github.com/h2zero/NimBLE-Arduino/tree/2.5.1), [license](docs/licenses/NimBLE-Arduino.txt) |

The Wi-Fi implementation uses the pinned Arduino-ESP32 framework. NimBLE-Arduino incorporates other components; their unmodified redistribution notices are included here for binary recipients:

| Included component | Notices and license |
| --- | --- |
| NimBLE-Arduino / esp-nimble-cpp | [NOTICE](docs/licenses/NimBLE-Arduino-NOTICE.txt), [Apache-2.0 license](docs/licenses/NimBLE-Arduino.txt) |
| Apache Mynewt NimBLE and Espressif modifications | [NOTICE](docs/licenses/Apache-Mynewt-NimBLE-NOTICE.txt), [license](docs/licenses/Apache-Mynewt-NimBLE-LICENSE.txt) |
| TinyCrypt and incorporated micro-ecc code | [copyright notices and BSD terms](docs/licenses/TinyCrypt-LICENSE.txt) |

These files are copied from NimBLE-Arduino 2.5.1's root `NOTICE`, `src/nimble/NOTICE`, `src/nimble/LICENSE` and `src/nimble/ext/tinycrypt/LICENSE`. They supplement the library's root license rather than changing it.

## Framework and SDK notices

Arduino-ESP32 2.0.17 includes the ESP-IDF 4.4.7 SDK. Core/library sources include copyright notices for the Arduino Team, Espressif Systems and their contributors. The release also carries these SDK component licenses:

| Component | Source / notice |
| --- | --- |
| ESP-IDF 4.4.7 | [source](https://github.com/espressif/esp-idf/tree/v4.4.7), [Apache-2.0 license](docs/licenses/ESP-IDF-Apache-2.0.txt) |
| FreeRTOS Kernel 10.4.3 | Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved. [ESP-IDF source](https://github.com/espressif/esp-idf/tree/v4.4.7/components/freertos), [MIT license](docs/licenses/FreeRTOS-LICENSE.txt) |
| lwIP | [SDK-pinned source](https://github.com/espressif/esp-lwip/tree/a45be9e438f6cf9c54ec150581819c3b95d5af6b), [copyright notice and BSD terms](docs/licenses/lwIP-COPYING.txt) |
| Espressif Wi-Fi library | [SDK-pinned source/distribution](https://github.com/espressif/esp32-wifi-lib/tree/f2aae4d44ec7908013066e69d29b9948846c335c), [Apache-2.0 license](docs/licenses/Espressif-WiFi-LICENSE.txt) |
| Espressif ESP32-C3/S3 Bluetooth controller library | [SDK-pinned distribution](https://github.com/espressif/esp32c3-bt-lib/tree/e5c0f7256ecf5b5f8eb28c1793051a6b88f95124), [Apache-2.0 license](docs/licenses/Espressif-Bluetooth-LICENSE.txt) |

The HTTPS implementation uses the SDK's existing HTTP client, Mbed TLS and built-in certificate bundle; no separate certificate bundle or additional JSON library is copied into this repository. Other framework/toolchain components retain their upstream licenses and source notices. PlatformIO obtains the pinned framework/toolchain packages separately; this repository does not vendor the complete SDK or compiler.

Developer tools are pinned in `requirements-dev.txt`: PlatformIO 6.1.19 (Apache-2.0) and pyserial 3.5 (BSD). Flashing instructions use esptool 4.5.1 (GPL-2.0-or-later) as a host utility. They are separate host-side programs, not code copied into the application.

The browser installer loads [ESP Web Tools 10.4.0](https://github.com/esphome/esp-web-tools/releases/tag/10.4.0), licensed under [Apache-2.0](https://github.com/esphome/esp-web-tools/blob/10.4.0/LICENSE), from a CDN. Its JavaScript runtime is not included in the firmware or OS download ZIPs.

## Hardware and behavior references

The minimal touch reader and packet decoder in `src/touch_input.*` and `lib/DeckCore/src/touch_packet.*` follow the CST328/CST3530 register and packet definitions in LILYGO's [Hynitron examples at `02b3e82a64926a1d9f907f3b5538a40d1e48795c`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/02b3e82a64926a1d9f907f3b5538a40d1e48795c/examples/touch_hyn_core/arduino). The upstream repository's [MIT notice](docs/licenses/LILYGO-touch-MIT.txt) is retained. No controller firmware image, update code, calibration routine or factory application is included.

- [LILYGO T-Deck Pro](https://github.com/Xinyuan-LilyGO/T-Deck-Pro), reference commit `53bcfa593b1df064894a8efc5a7e15e4524b09d2`: board pin definitions, modem startup examples, display and keyboard hardware facts.
- [LILYGO v1.1 / HD-V2-250915](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/02b3e82a64926a1d9f907f3b5538a40d1e48795c): v1.1 reset wiring and revision-specific peripherals.
- [Meshtastic TDeckProKeyboard.cpp](https://github.com/meshtastic/firmware/blob/master/src/input/TDeckProKeyboard.cpp): cross-check of physical keyboard event numbers, Space/Sym and modifiers. No complete Meshtastic implementation was copied, and the starter does not implement the Meshtastic protocol.
- [SIMCom A7682E](https://en.simcom.com/product/A7682E.html): specified RF bands and modem capabilities.
- [T-Mobile US network guidance](https://www.t-mobile.com/support/coverage/t-mobile-network): carrier spectrum context. A published band list is not certification of this device, its firmware or this SIM's voice service.

The firmware does not redistribute the original factory application, modem firmware, user SIM contents or factory flash dump.
