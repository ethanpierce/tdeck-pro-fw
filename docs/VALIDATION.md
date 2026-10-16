# Software validation and remaining hardware checks

The starter was subsequently built and flashed on the user's device on
2026-10-16. See [the local bench record](BENCH_2026-10-16.md) for two boot/USB
captures, physical Settings/light/APN checks, saved preferences after restart,
modem/SIM readiness, network scan/registration denial and
remaining hardware checks. The cloud preparation record below is historical.

Prepared on 2026-10-16 in a Linux cloud development environment. There was no physical T-Deck Pro connected. The software checks below support a flashable starter handoff; they do not establish that this device works as a US T-Mobile phone.

## Repeat the checks locally

With the Python environment active and a C++ compiler available:

```sh
bash tools/test.sh
pio run -e tdeck-pro
python -m py_compile tools/bench.py tools/package.py
```

The test runner builds and executes four host binaries with `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer is disabled by default because it cannot attach under some sandbox policies; address/undefined-behavior checking remains enabled.

## Additional local checks — 2026-10-16

The current host suites also passed three modem scenarios for APN apply/profile
reuse, rejection of APN injection and non-query commands, and a 90-second network
scan without reconnect. A fourth host binary tests APN validation, settings NVS
restart persistence, namespace isolation, failed-write rollback and corrupted or
truncated record recovery. Physical Settings/APN/light checks and their limits
are recorded in the bench report above.

## Checks exercised during preparation

| Check | Evidence and scope |
| --- | --- |
| Core regression suite | Passed 811 assertions plus 12,000 malformed SMS/radio packet pairs. Covers AT response/prompt parsing, unsolicited events, queue limits, SMS submission uncertainty, late responses, modem-reset recovery, streamed inbox responses, CEREG parsing, SMS PDU fixtures and bounds, malformed radio packets and keyboard modifier/event mapping. |
| Message-store suite | Passed 48 assertions against a host Preferences fake using the actual store code: 16-message retention, reboot recovery of pending sends, delivery updates, duplicate filtering, integrity-check rejection of corrupt records and RAM-only behavior after writes fail. |
| Cellular-service simulation | Passed six scenarios using the actual UART-owner loop with fake UART/FreeRTOS and a deterministic clock: slow modem boot and delayed SIM/storage readiness; hangup replacing an unsent dial; one-SMS gating and timeout uncertainty without resend; modem restart during SMS; a 100-message streamed SIM inbox; and slow call-state queries allowing SIM diagnostics/SMS setup to progress. This does not simulate RF, actual serial timing, flash, audio, or carrier behavior. |
| Firmware build | Passed using the pinned Espressif32 toolchain and T-Deck Pro ESP32-S3 profile, supplied partition table and vendored display driver. Application: 416,813 bytes; static RAM: 21,160 bytes. Heap/task/buffer allocations occur at runtime and are additional to static RAM. |
| USB capture utility | Passed a simulated pseudo-terminal serial-device test of `status`/`diag` transmission, response capture and logging. Python syntax and CLI help also checked. No actual board serial/USB behavior was established. |
| Documentation | Local Markdown links checked; hardware and radio defaults cross-checked against the documented vendor references |

The firmware build creates the app, bootloader, partition image and ELF. The final package's `firmware/BUILD_INFO.txt` identifies the source commit; `firmware/SHA256SUMS` identifies the included files. The merged `combined.bin` uses the documented ESP32-S3 offsets. Keep the matching committed source with any distributed firmware image.

## What still requires the device

- Correct PCB revision detection, power sequencing and battery/USB-only behavior.
- E-paper initialization/refresh, actual keyboard scan wiring and physical key legends.
- Modem AT response and firmware identity, SIM readiness, antenna/RF service and carrier registration.
- Real SMS receipt and transmission, error behavior and persistence on the ESP32's NVS.
- Carrier VoLTE/IMS eligibility, incoming/outgoing calls, microphone and speaker audio both ways.
- SX1262 transmit/receive with another compatible 915 MHz peer, including reception around e-paper refreshes.
- Charging behavior, current draw, temperature and battery life.

Use [NEXT_STEPS.md](../NEXT_STEPS.md) as the bench acceptance sequence and [FLASHING.md](FLASHING.md) for the original-flash backup before the first upload.
