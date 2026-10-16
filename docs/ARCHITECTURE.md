# Starter architecture

Handheld OS is ESP32-S3 firmware running on Arduino's FreeRTOS environment. It provides a small application shell suitable for bringing up this board's display, keyboard, cellular modem and LoRa radio. It does not boot Android or Linux.

```text
USB console ─┐
TCA8418 keys ├── main loop / e-paper UI ─── NVS message ring
             │          │
             │          ├── RadioService ─── SX1262 / shared SPI
             │          │
             └──────── action queue
                         │
                    cellular task ─── AT engine ─── UART1 / A7682E
                         │
                    event queue ─── main loop
```

## Ownership and responsiveness

`src/main.cpp` owns navigation, text entry, serial commands, display refresh requests and message updates. `src/board.cpp` owns the pin map and peripheral initialization. Keyboard and battery polling happen on the main task. The e-paper driver runs on that task too; partial refresh takes roughly 700 ms and typing is coalesced before redraw. A periodic full refresh reduces ghosting.

`src/modem_task.cpp` creates a dedicated cellular task on core 0. It owns UART1 and the AT engine, allowing modem bytes and unsolicited events to be processed while the main loop waits for an e-paper refresh. The main loop sends typed actions through a bounded FreeRTOS queue, and receives compact cellular events through another bounded queue. `status` exposes a dropped-event counter, which should remain zero during ordinary operation.

`src/radio_service.cpp` owns the SX1262. IRQ handling only sets a flag; the main loop completes transmission, reads incoming packets and restarts receive. The display and radio share SPI with separate chip selects and are accessed by the same task. The radio uses asynchronous transmit/receive, but long main-loop display refreshes can still affect how quickly a received packet is serviced; hardware tests must establish practical receive behavior.

## Host-testable core

`lib/DeckCore/src/` has no Arduino dependency:

- `at_engine.*`: bounded AT command scheduling, prompt/payload handling, responses, timeouts and unsolicited event handling.
- `sms_pdu.*`: number validation, single-part GSM-7 SMS submission encoding, supported SMS-DELIVER decoding and parsing helpers.
- `radio_packet.*`: strict validation of the compact plaintext Handheld OS broadcast format.
- `keyboard_decoder.*`: physical TCA8418 event decoding, modifiers and special keys.
- `connectivity.*`: bounded carrier-scan parsing, Wi-Fi credential validation, BLE address validation, HID report-map parsing and keyboard report decoding.

`tools/test.sh` compiles these with warnings as errors and address/undefined-behavior sanitizers. `tests/core_test.cpp` provides regression fixtures. A second suite compiles the actual `src/message_store.cpp` against a host fake of Preferences to exercise retention, corrupted records, reset handling and write failures. A third runs the actual cellular-service loop against a fake UART, FreeRTOS queues and deterministic clock to check boot/SIM recovery, SMS uncertainty, call scheduling and inbox streaming. These tests validate parsing and state behavior; they cannot establish hardware pin timing, radio link quality, carrier support or voice audio.

## Cellular behavior

On startup the modem is powered and the firmware probes for an AT link. Offline probes retry without repeatedly pulsing PWRKEY. Once the link is established, command echo is disabled and verbose errors plus registration/caller indications are requested. PDU mode, SIM SMS storage and inbox notifications are configured after `CPIN: READY`; transient setup failures are retried while the SIM is ready. Diagnostics query SIM, registration, signal and operator; full diagnostics also query model, firmware, function mode and serving system. The firmware does not place calls or send messages at boot.

New-SMS notifications fetch the indicated SIM slot; a SIM inbox scan also runs when setup first becomes ready and after reconnect. Supported received-message PDUs become history entries; sent/outbox PDUs are skipped. Unsupported multipart/sender/encoding cases are logged and remain on the SIM. Only SIM (`SM`) storage is supported. The firmware never automatically deletes SIM messages, so SIM storage can eventually fill.

SMS send requires the AT link, ready SIM, successful SMS setup and registration suitable for SMS. The firmware accepts `CEREG` states `1/5/6/7/9/10` for SMS and `1/5/9/10` for dialing. Emergency-only states are not ordinary service readiness. A returned `+CMGS` reference plus `OK` means “modem accepted.” There is no delivery-report tracking. Timeouts are treated as uncertain rather than retried automatically, since a modem may have sent the SMS before its response was lost. `diag` reconnects/reconfigures the link after the current operation completes.

Only one outbound SMS may be queued or active. Call requests use the modem's voice AT commands and poll current calls, including periodic checks outside a known call. Hangup uses `AT+CHUP`. Commands are serialized; a call operation cannot interrupt an SMS already being submitted. A newer pending call request replaces the previous one, so hangup can cancel a dial request that has not yet reached the modem. Voice transport eligibility, audio configuration and the physical audio path remain hardware acceptance work.

## Message persistence

`src/message_store.cpp` maintains a combined ring of the last 16 SMS/LoRa messages. Fixed-size records are stored in the `deckos` NVS namespace (retained for storage compatibility) with a magic/version marker and checksum. Invalid records are ignored at boot. A write failure changes the reported persistence state to RAM-only. Pending records loaded after a reset become “Result unknown,” since a reset cannot establish what the remote side received.

Incoming SMS uses a PDU fingerprint for duplicate detection; LoRa uses sender/sequence identity. The persisted 16-message ring suppresses duplicates within retained history, and the cellular task keeps an additional bounded in-session fingerprint list. This is bounded deduplication, not an infinite inbox archive or exactly-once delivery guarantee. The starter does not store message wall-clock timestamps.

## Device settings

`src/device_settings.cpp` stores APN and light choices in a separate
`decksettings` NVS namespace. A magic/version marker, bounded fields and checksum
reject corrupt records. Failed writes leave active settings unchanged and report
storage unavailable. Light control uses GPIO 42 for the keyboard and GPIO 45 for
the front light only on v1.1; on v1.0 that pin is touch reset.

The Settings app is a native keyboard/e-paper screen opened with Home → S.
APNs are validated before storage and before entering an AT command. Only the
cellular task writes UART1: it applies a saved APN to context 1 as `IPV4V6` when
idle/ready, first checking whether the modem already has the desired profile.
No packet-data session is started. A bounded allowlist permits explicit read-only
network diagnostics; operator scanning has a three-minute command timeout.

## Wi-Fi and Bluetooth

`src/wifi_service.cpp` runs asynchronous scans and polls station connection state
on the main loop. `WiFi.persistent(false)` avoids a second framework credential
store; a separate versioned/checksummed `wireless` record in `decksettings` holds
the selected network, credentials, enable switches and keyboard identity address.
The original APN/light record is preserved across the upgrade. Passwords are
masked and excluded from serial/BLE status output.

`src/ble_service.cpp` owns NimBLE lifecycle, central connections, GATT discovery
and phone services on a worker task. BLE host callbacks emit bounded queue events;
UI mutation and HID keystroke decoding occur only on the main task. Pairing
prompts reach the display and require physical confirmation or passkey input;
there is no automatic numeric-comparison approval. Phone status reads/writes
require authenticated encryption. Phone visibility is explicitly requested and
expires after two minutes. Keyboard report maps and input values are bounded;
unsupported reports are rejected. BLE bond keys are managed by NimBLE in NVS.
Actual keyboard/phone pairing was deferred by the user after passive BLE
advertising was checked.

## Configuration and extension points

`config/settings.h` supplies 915 MHz radio parameters, automatic board revision detection and modem baud. Ignored `config/local.h` may override defaults with `#define`. Pin changes belong in the board profile, with a reference to the matching schematic.

Add future apps through the main UI once hardware bring-up is stable. Before expanding scope, choose how to handle message deletion/export, Unicode fonts, timekeeping, touch, audio, power management and radio interoperability. A switch to Meshtastic or another stack requires integration of that stack and its protocol; changing a sync word alone does not provide compatibility.
