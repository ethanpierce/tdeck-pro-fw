# Validation

The native firmware uses ESP-IDF 5.4.0 and FreeRTOS, with no Arduino component.
Physical checks and their exact firmware revisions are recorded in the
[dated bench log](BENCH_2026-10-16.md). A host test or successful build does not
establish radio reception, carrier eligibility, voice audio or battery life.

## Repeat the software checks

Use the pinned Python environment and a C++17 compiler:

```sh
bash tools/test.sh
pio run -e tdeck-pro
python tools/release.py --self-test
```

The host runner builds eleven binaries with `-Wall -Wextra -Werror`,
AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer is disabled by
default where sandbox attachment is unavailable; the other sanitizers remain on.

| Suite | Coverage |
| --- | --- |
| Core | AT response/prompt/URC parsing, queue and timeout behavior, SMS uncertainty, reset recovery, streamed inbox, SMS/radio codecs, keyboard modifiers; 12,000 malformed SMS/radio pairs |
| Message store | Actual store and native NVS adapter, 16-record retention, pending-send recovery, duplicate suppression, corruption and write failures |
| Cellular service | Actual UART-owner loop with a fake UART/FreeRTOS clock: 30 scenarios including delayed SIM setup, storage fallback/restoration, both-store import, backpressure, TCP lease/reset/expiry, warm two-turn reuse, strict TCP mode queries and interleaved notifications |
| Settings | APN validation, existing blob formats, saved settings across restart, isolated namespaces, wireless preferences and failed-write/corruption handling |
| Connectivity | Carrier parsing, Wi-Fi limits, signal indicators, BLE addresses and HID array/NKRO decoding; 6,000 malformed inputs |
| AI | HTTPS/configuration rules, JSON escaping, model profiles, bounded history, 500 Unicode character replies, secret-safe errors and NVS rollback; 6,000 malformed replies |
| Conversations | Peer normalization/reply preservation, broadcast scope, ordering and bounded UTF-8 previews; 6,000 malformed previews |
| Touch | Controller-specific packets, checksum, coordinate/press/release validation; 6,000 malformed packets |
| LTE HTTP | HTTP framing and bounds, HEX socket framing, UTC clock, exact binary AT payload handling; 4,000 malformed responses |
| UI input | Immediate navigation, 100 ms quiet/250 ms burst bound, dirty-generation races, clock wrap, busy-key FIFO and fail-closed overflow |
| Native display | Framebuffer/font clipping and polarity, full/partial command/window/plane sequences, bulk SPI transfer, busy callback, timeout and error cleanup |

The native build passed for ESP32-S3, DIO 16 MiB flash, QSPI PSRAM, USB
Serial/JTAG and the existing partition table. Production builds use `-O2`; assertions,
watchdogs, TLS verification, stack sizes and task affinity remain enabled/configured
as before the optimization switch. It links only native SDK services
and RadioLib's portable C++ HAL. Compiler inputs were checked for Arduino,
NimBLE-Arduino and Adafruit runtime include paths; none were present.

Release fixtures check native SDK identity, chip/image headers, partition
offsets, the untouched NVS gap, initial OTA metadata, exact-commit source ZIPs,
OS-package checksums, installer erase defaults and restoration of local build
configuration after failure. Release builds regenerate from committed
`sdkconfig.defaults` so a maintainer's menuconfig changes cannot silently alter
the public image. Packages use four separate images; no padded merged flash
image is distributed.

## Hardware acceptance still required

Consult the bench log for completed checks. Outstanding work includes inbound
SMS, full keyboard/touch coverage, BLE keyboard/phone pairing (deferred by the
user), a second LoRa peer, full audio-routing/quality coverage, battery-only operation and power/current
measurements. Native-driver regression checks must be distinguished from
successful tests of earlier firmware.

Keep private backups, credentials, SIM contents, camera captures and raw logs
outside source/release archives. Use [NEXT_STEPS.md](../NEXT_STEPS.md) for the
remaining bench sequence and [FLASHING.md](FLASHING.md) for backup/recovery.
