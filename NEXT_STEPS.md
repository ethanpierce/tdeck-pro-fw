# Next hardware checks

The user resumed **power-off work only**. BOOT 5-second clean shutdown,
30-second forced radio-off deep sleep, RESET wake policy, Settings confirmation
and the Shutting down / Powered off screens are implemented and flashed.
The user confirmed both physical hold paths, shutdown screens and RESET wake.
The final display-power correction is flashed and clean USB shutdown passed;
the device was left asleep. Battery-only behavior and current measurements
remain unverified. See [POWER_OFF.md](docs/POWER_OFF.md).

All subagents remain stopped. Mesh/GPS peer/outdoor tests, broader UI acceptance
and release publication remain outstanding; no automatic follow-up is scheduled.
Read [the remaining checkpoint/test plan](docs/DEVELOPMENT_PAUSE.md).

Handheld OS uses native ESP-IDF 5.4.0 and FreeRTOS; Arduino has been removed.
The original flash and private settings were backed up before flashing. Read the
[dated bench record](docs/BENCH_2026-10-16.md) for checks tied to an exact image
and [FLASHING.md](docs/FLASHING.md) before another upload.

## Current source and acceptance

Home has Messages, Phone and Settings. Messages has SMS conversations, AI and a
Meshtastic channel group; Phone has a touch keypad and call controls. Settings
includes Airplane mode, connections, messaging, lights, location and device
status. The new UI, native Meshtastic leaf client and GPS support require the
combined-image checks below; earlier successes do not establish these new flows.

1. Check all touch targets, full international phone-number display, conversation
   scrolling, rapid keyboard input and back navigation. Never place a call or
   send an SMS merely to exercise a control without a consenting recipient.
2. Enable Airplane mode and verify actual cellular/Wi-Fi/BLE/LoRa/GPS shutdown,
   blocked sends/scans, saved state across restart, and restoration of each prior
   preference. Interrupted sends must never replay. Keep BLE disabled: the user
   deferred accessory pairing tests.
3. Enable GPS outdoors, verify a fresh satellite fix and compare the displayed
   coordinates. Share by SMS should prepare a Google Maps link, ask for a recipient
   and require explicit Send. Do not mistake parser fixtures for satellite reception.
4. Use a second Meshtastic device with matching US region, preset, channel and key
   for messages in both directions and node discovery. The native subset is a
   non-relaying channel client, without PKI direct messages or delivery receipts.
   Region remains unset and mesh off until explicitly configured. See [MESH.md](docs/MESH.md).
5. Measure internal heap minima with a full message history, AI requests and
   repeated radio transitions, then battery current, runtime and temperature.
   Record image size and static RAM from the actual build output.

## Established baseline

On the optimized native image preceding the latest feature work, the user
confirmed touch and rapid physical typing. Camera inspection showed the full
phrase in the AI composer. Wi-Fi rejoined, AT&T LTE Band 5 registered with the
saved APN, and multiple AI requests passed over both Wi-Fi and LTE, including
warm LTE reuse after switching Wi-Fi off. Saved credentials survived uploads
and resets. The user also confirmed a working phone call; the modem showed
Dialing, Ringing, Active and Idle. Individual audio routes and quality remain
unmeasured. Local service success does not establish carrier certification or
coverage elsewhere.

A consenting recipient received the earlier outbound SMS. Fresh incoming
replies have not been found despite inspection of both stores. SIM remains full
at 5/5 and modem memory empty at 0/180, with SM/SM/ME, PDU mode, CNMI and URC routing
verified. The full SIM is not a confirmed explanation for empty modem reception.
Keep all carrier messages; no automatic deletion or new test send is authorized.

BLE keyboard/phone pairing remains deferred. CST3530/v1.1 touch was exercised;
CST328/v1.0 hardware, complete voice/audio coverage, cold-power operation,
battery life and Windows hardware flashing remain separate acceptance work.

## Release and privacy

The macOS/Windows browser-installer packages and exact-commit source packaging
are prepared. Publish a clearly marked prerelease only after its build, source,
checksums and installer are verified; Windows hardware operation must remain
labelled unverified until tested. Use [INSTALL_MACOS.md](docs/INSTALL_MACOS.md)
and [INSTALL_WINDOWS.md](docs/INSTALL_WINDOWS.md) for the intended installation
flow. Confirm the hosted installer URL is live before announcing a release.

Keep API keys, Wi-Fi credentials, SIM/device identifiers, backups, raw logs,
camera captures, local settings and the original private Git branch out of the
public repository and source archives. Preserve existing NVS and private
recovery backups. See [VALIDATION.md](docs/VALIDATION.md),
[CONNECTIVITY.md](docs/CONNECTIVITY.md), [AI_CHAT.md](docs/AI_CHAT.md) and
[GPS.md](docs/GPS.md) for feature limits and setup.
