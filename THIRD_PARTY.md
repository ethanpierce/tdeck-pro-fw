# Third-party code and hardware references

Handheld OS is distributed under GNU GPL version 3 (`GPL-3.0-only`); see
[LICENSE](LICENSE). Components retain their own copyright and license notices.
The firmware builds native C/C++ with ESP-IDF. It does not use the Arduino
framework, Adafruit device libraries or NimBLE-Arduino.

## Display and font adaptations

`src/native_screen.cpp` adapts the GDEQ031T10 / UC8253 panel command protocol from
Jean-Marc Zingg's GPL-3.0 GxEPD2 driver, as carried by LILYGO at commit
[`53bcfa593b1df064894a8efc5a7e15e4524b09d2`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/53bcfa593b1df064894a8efc5a7e15e4524b09d2/lib/GxEPD2).
The implementation uses native SPI/GPIO and the panel's existing OTP waveforms.
The original [GPL license](lib/GxEPD2/LICENSE) is retained. The old Arduino driver
is not a build dependency.

`lib/DeckCore/src/font5x7.h` retains the ASCII portion of Adafruit GFX 1.11.11's
`glcdfont.c`, copyright Adafruit Industries, under its [BSD terms](docs/licenses/Adafruit-GFX.txt).
The native framebuffer/canvas does not link Adafruit GFX or its runtime.
The native keyboard register initialization follows Adafruit TCA8418 1.0.2
under its [BSD-3-Clause terms](docs/licenses/Adafruit-TCA8418.txt); no Adafruit
BusIO or framework runtime is linked.

## Pinned build dependencies

The exact direct dependency versions are in `platformio.ini`:

| Component | Version | License and source |
| --- | --- | --- |
| PlatformIO Espressif32 | 6.10.0 | Apache-2.0; [source](https://github.com/platformio/platform-espressif32/tree/v6.10.0) |
| ESP-IDF | 5.4.0 (`framework-espidf@3.50400.0`) | Apache-2.0; [source](https://github.com/espressif/esp-idf/tree/v5.4), [license](docs/licenses/ESP-IDF-Apache-2.0.txt) |
| RadioLib | 7.1.2 | MIT; [source](https://github.com/jgromes/RadioLib/tree/7.1.2), [license](docs/licenses/RadioLib.txt) |

RadioLib uses its portable C++ HAL over the project's native SPI/GPIO layer.
The SDK supplies Wi-Fi, Bluetooth LE, FreeRTOS, TCP/IP, NVS, HTTP and TLS.
The following notices were copied from the pinned ESP-IDF 5.4 package:

| SDK component | License / notice |
| --- | --- |
| FreeRTOS kernel | [MIT terms](docs/licenses/FreeRTOS-LICENSE.txt) |
| lwIP | [BSD terms](docs/licenses/lwIP-COPYING.txt) |
| Espressif Wi-Fi library | [Apache-2.0 license](docs/licenses/Espressif-WiFi-LICENSE.txt) |
| Espressif ESP32-C3/S3 Bluetooth controller | [Apache-2.0 license](docs/licenses/Espressif-Bluetooth-LICENSE.txt) |
| Apache Mynewt NimBLE | [NOTICE](docs/licenses/Apache-Mynewt-NimBLE-NOTICE.txt), [Apache-2.0 license](docs/licenses/Apache-Mynewt-NimBLE-LICENSE.txt) |
| TinyCrypt / incorporated micro-ecc | [copyright and BSD terms](docs/licenses/TinyCrypt-LICENSE.txt) |
| Mbed TLS | [Apache-2.0 terms](docs/licenses/MbedTLS-LICENSE.txt) |

NimBLE/TinyCrypt notices originate from
`components/bt/host/nimble/nimble/`; other licenses are from the corresponding
SDK component directories. SDK-pinned submodule sources are available through
[ESP-IDF v5.4](https://github.com/espressif/esp-idf/tree/v5.4). Other SDK/toolchain
components retain their upstream notices. This repository does not vendor the
complete SDK or compiler.

The HTTPS implementation uses ESP-IDF's HTTP client, Mbed TLS and built-in CA
bundle. No separate JSON library or modem TLS implementation is included.

## Meshtastic protocol compatibility

The bounded native channel-messaging implementation follows Meshtastic firmware
[`54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`](https://github.com/meshtastic/firmware/tree/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb)
and protobuf definitions
[`6b1ded439633cd03d4af85b44231b91d1d106278`](https://github.com/meshtastic/protobufs/tree/6b1ded439633cd03d4af85b44231b91d1d106278),
under their [GPL-3.0 terms](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/LICENSE).
Copyright remains with the Meshtastic contributors. The packet, channel,
frequency and cryptographic protocol adaptations are distributed under this
project's GPL-3.0-only license. No Meshtastic Arduino runtime is linked.
AES uses the existing native Mbed TLS dependency. Scope, source references and
interoperability limits are documented in [MESH.md](docs/MESH.md).

Meshtastic is the upstream project's name. Handheld OS is an independent
firmware project; protocol compatibility does not imply upstream endorsement.

Developer tools are pinned in `requirements-dev.txt`: PlatformIO 6.1.19
(Apache-2.0) and pyserial 3.5 (BSD). Esptool is a separate GPL-2.0-or-later
host-side flashing utility. These programs are not linked into the firmware.

The browser installer loads [ESP Web Tools 10.4.0](https://github.com/esphome/esp-web-tools/releases/tag/10.4.0)
from a CDN under its [Apache-2.0 license](https://github.com/esphome/esp-web-tools/blob/10.4.0/LICENSE).
Its runtime is not included in the firmware or OS download ZIPs.

## Corresponding release source

Each release includes a source ZIP made from its exact public Git commit, the
build scripts, board definition, configuration, dependency pins and license
notices. `BUILD_INFO.json` records that commit and image hashes. The release tag
and pinned upstream repositories provide the corresponding source; private Git
branches, local settings and device backups are excluded.

## Hardware and behavior references

The touch reader and packet decoder follow the CST328/CST3530 definitions in
LILYGO's [Hynitron examples at `02b3e82a64926a1d9f907f3b5538a40d1e48795c`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/02b3e82a64926a1d9f907f3b5538a40d1e48795c/examples/touch_hyn_core/arduino).
The upstream [MIT notice](docs/licenses/LILYGO-touch-MIT.txt) is retained. No
controller firmware, update image or calibration routine is included.

- [LILYGO T-Deck Pro](https://github.com/Xinyuan-LilyGO/T-Deck-Pro), reference commit `53bcfa593b1df064894a8efc5a7e15e4524b09d2`: pin definitions, panel protocol, modem startup and keyboard hardware.
- [LILYGO v1.1 reference](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/02b3e82a64926a1d9f907f3b5538a40d1e48795c): revision-specific wiring.
- [Meshtastic keyboard implementation](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/src/input/TDeckProKeyboard.cpp): cross-check of physical event numbers and modifiers.
- [SIMCom A7682E](https://www.simcom.com/product/A7682E.html): modem capabilities and RF bands. Band overlap does not establish carrier certification or service eligibility.

The original factory application, modem firmware, user SIM contents, credentials
and private flash dumps are not redistributed.
