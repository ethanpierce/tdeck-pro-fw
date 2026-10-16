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

The Messages launcher opens a unified conversation list. The host-testable
conversation index groups numeric SMS peers with an optional leading `+`, keeps
the latest original reply address and orders peers by their latest stored
sequence. All radio messages share one Nearby LoRa broadcast thread; the UI adds
one AI thread separately. The index handles at most the newest 16 valid history
records and bounded UTF-8 previews. It does not invent contacts, unread state or
private radio conversations. Threads use an inline composer, a separate Send
hit area, Older/Newer controls and Shift + Alt + K/J scrolling.

`src/touch_input.cpp` shares the UI task's I2C owner. It recognizes CST328 or
CST3530 only after controller identification and a 240×320 resolution check.
Bounded packets and coordinate validation live in `touch_packet.*`. The main
loop uses press transitions for taps and discards touches received during a
blocking redraw, preventing an old row from activating a newly drawn Send area.
This implementation still needs physical touch/coordinate validation.

`src/modem_task.cpp` creates a dedicated cellular task on core 0. It owns UART1 and the AT engine, allowing modem bytes and unsolicited events to be processed while the main loop waits for an e-paper refresh. The main loop sends typed actions through a bounded FreeRTOS queue, and receives compact cellular events through another bounded queue. `status` exposes a dropped-event counter, which should remain zero during ordinary operation.

`src/radio_service.cpp` owns the SX1262. The main loop polls its latched DIO1 interrupt pin, completes transmission, reads incoming packets and restarts receive. No GPIO ISR is installed: this avoids the ESP32-S3 framework's small IPC stack during interrupt setup. The display and radio share SPI with separate chip selects and are accessed by the same task. The radio uses asynchronous transmit/receive, but long main-loop display refreshes can still affect how quickly a received packet is serviced; hardware tests must establish practical receive behavior.

## Host-testable core

`lib/DeckCore/src/` has no Arduino dependency:

- `at_engine.*`: bounded AT command scheduling, prompt/payload handling, responses, timeouts and unsolicited event handling.
- `sms_pdu.*`: number validation, single-part GSM-7 SMS submission encoding, supported SMS-DELIVER decoding and parsing helpers.
- `radio_packet.*`: strict validation of the compact plaintext Handheld OS broadcast format.
- `keyboard_decoder.*`: physical TCA8418 event decoding, modifiers and special keys.
- `connectivity.*`: bounded carrier-scan parsing, Wi-Fi credential validation, BLE address validation, HID report-map parsing and keyboard report decoding.
- `conversations.*`: bounded numeric-peer grouping, reply-address preservation, sequence ordering and UTF-8 previews.
- `touch_packet.*`: controller-specific packet/coordinate validation.
- `ai_protocol.*`: bounded provider requests, system prompt, response validation and the 500-character reply limit.
- `cellular_http_protocol.*`: strict HEX decoding, modem clock parsing, HTTPS address validation and bounded HTTP response framing.

`tools/test.sh` compiles these with warnings as errors and address/undefined-behavior sanitizers. `tests/core_test.cpp` provides regression fixtures. A second suite compiles the actual `src/message_store.cpp` against a host fake of Preferences to exercise retention, corrupted records, reset handling and write failures. A third runs the actual cellular-service loop against a fake UART, FreeRTOS queues and deterministic clock to check boot/SIM recovery, SMS uncertainty, call scheduling and inbox streaming. These tests validate parsing and state behavior; they cannot establish hardware pin timing, radio link quality, carrier support or voice audio.

## Cellular behavior

On startup the modem is powered and the firmware probes for an AT link. Offline probes retry without repeatedly pulsing PWRKEY. Once the link is established, command echo is disabled and verbose errors plus registration/caller indications are requested. PDU mode, SMS storage and inbox notifications are configured after `CPIN: READY`; transient setup failures are retried while the SIM is ready. Diagnostics query SIM, registration, signal and operator; full diagnostics also query model, firmware, function mode and serving system. The firmware does not place calls or send messages at boot.

SMS storage prefers `SM/SM/ME`: default reads and stored-message sending use SIM,
while new messages use modem memory, avoiding a full SIM's receiving limit.
Unsupported ME reception falls back to all-SM with a warning. Boot, reconnect
and manual inbox import schedule both stores. SM notifications read their slot
when safe or defer a scan; ME notifications schedule a streaming scan. ME scans
temporarily change only mem1 and restore SM before any SIM slot read. Generation
checks, bounded retry intervals and storage-readiness gating handle resets,
timeouts, restore failures and UI queue backpressure. Supported received-message
PDUs enter history; sent/outbox PDUs are skipped. Unsupported messages remain in
their original store. Neither store is automatically deleted; a full receiving
store can block delivery. Automatic scans omit raw PDUs, while the explicit
`modem inbox-me` diagnostic may print them and mark messages read.

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
No provider request is sent by saving settings or booting. An explicit LTE AI
request may open packet data. A bounded allowlist permits explicit read-only
network diagnostics; operator scanning has a three-minute command timeout.

## AI worker and LTE transport

`src/ai_chat.cpp` owns a bounded worker request/result queue and RAM-only
conversation history. Connected Wi-Fi is preferred; otherwise an explicit send
uses ready LTE service. The transport is chosen once and failures are never
automatically replayed. A fixed system prompt requests short plain-text answers;
UTF-8-aware parsing enforces 500 Unicode characters plus separate byte/body caps.
Four fixed OpenAI model presets and a Custom ID are convenience choices, not a
live discovery of the account's accessible models.

Wi-Fi uses ESP-IDF's verified HTTPS client. For LTE, `src/cellular_http.cpp` runs
mbedTLS on the ESP32 with required CA-chain and hostname verification using the
SDK certificate bundle. A valid clock is required. Native modem HTTPS/TLS
commands are unused. `src/cellular_socket.cpp` brokers bounded TCP operations
through queues; the AI worker never reads or writes UART1. Requests retain
ownership after caller timeouts so a late modem completion cannot access freed
caller buffers, and expired queued jobs do not start stale network operations.

The AT engine supports fixed-length binary transmit payloads without the SMS
Ctrl-Z terminator. Socket reads request bounded HEX data, decode only the declared
count and keep encrypted bytes containing AT-like text from becoming false
URCs. Socket acknowledgements and asynchronous results are matched to the
reserved connection. SMS notifications remain with the cellular service and
commands stay serialized; disruptive diagnostics/APN/network changes are refused
during a transport lease. HTTP framing, body limits, authenticated close handling
and secret-safe errors bound the response path. Host simulations cover framing,
queued-job expiry, interleaved SMS notifications and reset cancellation; live
LTE/TLS operation remains a hardware acceptance check.

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

Add future apps through the main UI once hardware bring-up is stable. Before expanding scope, choose how to handle message deletion/export, Unicode fonts, timekeeping, additional touch gestures, audio, power management and radio interoperability. A switch to Meshtastic or another stack requires integration of that stack and its protocol; changing a sync word alone does not provide compatibility.
