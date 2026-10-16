# Handheld OS

> Power-button shutdown work resumed on 2026-10-16. The native build is flashed;
> the new Mesh/GPS features still require their physical acceptance tests.
> See [power-off controls](docs/POWER_OFF.md) and the [remaining test plan](docs/DEVELOPMENT_PAUSE.md).

A touch and keyboard firmware for the **LILYGO T-Deck Pro with an A7682E modem and 915 MHz SX1262 radio**. It runs on the ESP32-S3 using native ESP-IDF 5.4 and FreeRTOS, with e-paper conversations for SMS, AI and a Meshtastic channel, a touch dialer, settings and USB diagnostics.

**The first public firmware release and hosted installer are pending validation.** Browser installation and simple macOS/Windows download packages are prepared; build from source below until they are published.
[macOS instructions](docs/INSTALL_MACOS.md) · [Windows instructions](docs/INSTALL_WINDOWS.md)

An independent hobby project. Hardware and carrier names identify compatible equipment and services; they do not imply affiliation or endorsement.

**The A7682E is a poor match for T-Mobile in the United States.** SIMCom specifies LTE bands 1/3/5/7/8/20; it lacks T-Mobile's B2/B4/B12/B66/B71. B5 provides only limited possible overlap. Firmware and APN changes cannot add missing radio bands. LTE registration, SMS service, and especially VoLTE must be checked on the actual device before treating it as a phone. See [the hardware notes](docs/HARDWARE.md).

This is experimental hobby firmware. **The native ESP-IDF/FreeRTOS build was flashed and bench-tested on a v1.1 device on 2026-10-16.** Display, physical keyboard/touch navigation, settings/key retention, Wi-Fi reconnection, LoRa initialization and AT&T LTE Band 5 registration passed. Native AI replies worked over Wi-Fi and LTE; the optimized build also completed consecutive LTE chat requests with Wi-Fi off. The user confirmed a phone call worked. One outbound SMS was delivered during earlier testing. Incoming SMS, BLE keyboard/phone pairing, a second LoRa peer, full audio-routing/quality coverage and power/battery-life measurements remain incomplete. The earlier T-Mobile SIM did not register. The latest simplified interface, airplane-mode controls, GPS/location sharing and Meshtastic leaf-client changes still need physical acceptance testing. Public release is held while these changes are checked. See the [bench record](docs/BENCH_2026-10-16.md) and [NEXT_STEPS.md](NEXT_STEPS.md).

## Included

| Feature | Starter behavior |
| --- | --- |
| Home | Three destinations: Messages, Phone and Settings |
| Status bar | Wi-Fi state with three bars, cellular technology with four bars, and battery percentage |
| Conversations | One SMS thread per numeric peer, one AI thread, and one shared Meshtastic channel thread; inline composer |
| SMS | Compose/send a single GSM-7 message; prefer modem memory for reception, with SIM fallback; import supported messages from both stores |
| AI Chat | HTTPS over Wi-Fi or LTE, both bench-tested; four model presets plus Custom; replies limited to 500 characters |
| Touch | Supported CST328/CST3530 240×320 controllers; CST3530 detection and the Messages/AI/Send flow passed; remaining controls need testing |
| Phone | Numeric touch keypad, complete phone-number preview, Call/Answer/End controls and call-state polling |
| Meshtastic | Bounded native leaf client for encrypted channel text and node information; off until configured with an explicit US region |
| History | Last 16 combined SMS/LoRa messages in NVS, with integrity checks and duplicate filtering |
| Settings | Power off confirmation, Airplane mode, Connections, Messaging, Display, Location and About categories |
| Location | Receive-only GPS, fix/satellite status and explicit SMS sharing of a captured Google Maps link |
| Wi-Fi | 2.4 GHz scan, network/password entry, DHCP connection, saved credentials and forget |
| Bluetooth LE | Keyboard discovery/HID host, authenticated phone companion service and bond controls; pairing bench tests deferred |
| Diagnostics | USB commands, SIM/registration/signal/modem queries, keyboard tracing and log capture |

Meshtastic support is a limited native leaf client, rather than the full Meshtastic firmware. Match region, preset, channel and key on both devices. The default channel key is public and provides no privacy. This implementation does not claim forwarding, private/direct messages, PKI, delivery acknowledgements, a phone protocol or LoRaWAN. Peer interoperability is pending a second-device test. The previous custom plaintext radio protocol is replaced.

No SMS, call, AI request, location message or mesh announcement is sent automatically at startup. Enabled cellular service registers with the network; enabled mesh listens. GPS starts off and only receives satellite data when enabled. Airplane mode suspends cellular, Wi-Fi, BLE, mesh and GPS while retaining their saved preferences. This project targets T-Deck Pro v1.0/v1.1; T-Deck Pro MAX is unsupported.

## Build and flash

Use Python 3.12, Git and a C++ compiler (`g++` or `clang++`) for the host tests. The first firmware build downloads the pinned PlatformIO tools, native ESP-IDF 5.4 SDK and RadioLib. The Arduino framework, Adafruit device wrappers and NimBLE-Arduino are not used.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
bash tools/test.sh
pio run -e tdeck-pro
```

On Windows PowerShell, use `py -3.12 -m venv .venv` and `.venv\Scripts\Activate.ps1`; the firmware build works there directly. Run the Bash/host compiler tests in WSL or an equivalent C++ environment.

**Back up the original 16 MiB flash before the first upload.** Follow [docs/FLASHING.md](docs/FLASHING.md) for backup, bootloader entry, port selection, upload and restoration. With the backup complete:

```sh
python tools/bench.py --list
pio run -e tdeck-pro -t upload --upload-port /dev/ttyACM0
python tools/bench.py --port /dev/ttyACM0 --seconds 60
```

Replace the port with the actual device, such as `COM5` on Windows. PlatformIO uploads the bootloader, partition table and app at the correct addresses. A standalone `firmware.bin` is only the app, not a complete first-flash image.

## Messages and controls

Home has **Messages**, **Phone** and **Settings**. Tap a destination, use its M/P/S shortcut, or select with J/K and Enter. **Sym** or **< Back** returns to the previous screen; **Home** in the bottom-right returns to Home.

Messages contains the AI Assistant, the shared Meshtastic channel and SMS conversations grouped by phone number. **New** asks for an SMS recipient; Continue opens a draft without sending. In a thread, type into the composer and press Enter or tap the separate **Send** button. Ordinary N/J/K/C keys remain message text. Older/Newer or Shift+Alt+K/J scrolls history. The **...** menu opens AI or channel settings. Leaving a thread cancels its draft; an already submitted request continues unless airplane mode cancels its radio operation. E-paper keeps its normal panel timings, coalesces typing and rejects taps against a stale image.

| Screen | Controls |
| --- | --- |
| Home | M Messages, P Phone, S Settings; J/K and Enter also work |
| Conversations | J/K selects, Enter opens; N or New starts an SMS |
| Message thread | Type immediately; Enter or Send submits; Older/Newer scroll |
| Phone | Touch digits or type the printed number legends; Call/Answer/End are separate buttons; A answers an incoming call, H ends a call |
| Number entry | W/E/R → 1/2/3, S/D/F → 4/5/6, Z/X/C → 7/8/9, P → 0, O → + |
| Settings | J/K and Enter or tap a row; A Airplane, C Connections, M Messaging, D Display, L Location, I About |
| Connections | Wi-Fi, Bluetooth, Cellular; Wi-Fi and BLE have touch toggles, scan and setup controls |
| Messaging settings | AI model/key/endpoint, or Meshtastic region/preset/channel/key and heard nodes |
| Location | G or GPS toggles reception; S or Share by SMS prepares a captured-location draft |

The touch dialer shows the whole valid international number before calling, using smaller text when needed. Carrier service and the device's audio path still determine call capability. Keypad service codes and in-call DTMF are not implemented.

Hold `Shift` for uppercase and `Alt` for the printed alternate symbols when composing. The microphone key is not a voice-input feature. The e-paper display coalesces typing and refreshes slowly; USB output provides a quicker view while debugging.

Outbound SMS accepts printable ASCII characters available in GSM-7, up to 160 septets; extension characters such as `{`, `}` and `\` each consume two. Incoming single-part GSM-7 and UCS-2 messages from numeric senders are supported. Multipart messages, alphanumeric senders and other encodings remain in their original SIM or modem store. New reception prefers modem (ME) storage so a full SIM does not consume all receiving space; setup falls back to SIM storage if ME reception is unavailable. Both stores are scanned at boot and by the inbox import control. UTF-8 text is retained in history and USB output; unsupported e-paper font characters appear as `?`.

“Modem accepted” confirms an SMS submission, not delivery to the recipient. “Transmitted” confirms the mesh radio completed transmission, not reception by another device. An interrupted operation may become “Result unknown”; check the receiving device before manually retrying. Stored SMS messages are never automatically deleted; a full receiving store can block new delivery.

## Settings

Hold **BOOT for 5 seconds** to start a clean shutdown. The screen shows
**Shutting down**, then **Powered off — Press RESET to start again**.
Release BOOT, then press RESET when you want to restart. A continuous
**30-second total hold** forces radio-off deep sleep through an independent
monitor if cleanup or the UI is stuck. This is not physical battery isolation.
Settings also has a top-right **Power off** button with confirmation (keyboard:
**O**). See [shutdown behavior and hardware limits](docs/POWER_OFF.md).

**Airplane mode** powers down or suspends all five radios. Its status distinguishes shutdown in progress from confirmed off. Radio controls and sends are unavailable while it is active; disabling airplane mode restores saved preferences. A request interrupted by airplane mode is not automatically replayed.

**Connections** contains Wi-Fi, Bluetooth and Cellular. Cellular includes the APN editor, carrier scan and status refresh. Use your carrier's APN; a working APN cannot add radio bands or establish carrier eligibility. Wi-Fi supports saved 2.4 GHz networks. BLE provides keyboard discovery and an authenticated status-only phone companion; Bluetooth Classic keyboards are incompatible. Pairing hardware tests remain deferred.

**Messaging → AI** contains model, masked API key and HTTPS endpoint fields. The four presets are GPT-5.6 Luna, Terra, Sol and GPT-5.4 Mini; Custom accepts another provider model ID. A preset does not establish account access. AI prefers connected Wi-Fi and otherwise uses ready LTE; both paths verify HTTPS certificates and hostname. No automatic retry switches connections. Device keys stay in NVS, and AI conversations stay in RAM. Keys and full-device backups are private; the optional [USB browser setup](installer/ai-setup.html) lets you paste a long key when served locally over localhost or HTTPS. Its public hosted page will accompany the release. See [AI setup](docs/AI_CHAT.md).

**Messaging → Meshtastic** starts off with no region selected. Select United States only where appropriate for this 915 MHz hardware, then match preset, channel and key with your other device before enabling. Preset changes update a matching default channel name while preserving a custom name. Custom channel keys are entered as 32 or 64 hexadecimal characters and remain hidden; choosing the public default is explicit. Heard nodes lists nearby devices, with a separate explicit Announce action. Messages use one shared channel group. Channel encryption has no authentication tag; a custom key is not a delivery or sender-authenticity guarantee.

**Display** controls the screen and keyboard lights; the front light is fitted on v1.1. **Location** controls receive-only GPS and shows fix/satellite status. Share by SMS requires a valid fix no more than 30 seconds old, asks for a recipient, then opens an editable message with a captured Google Maps link and capture time when available. It never sends automatically or changes the location after review. GPS reception and location-message delivery still need outdoor/recipient testing.

Saved SMS/mesh history holds 16 combined messages. The screen limits rendering to the most recent 300 wrapped lines to keep memory use bounded; full stored text remains available through the private USB inbox command. **About** shows concise device and connection status, with detailed diagnostics available over USB.

## USB console

Open at 115200 baud with newline endings, using `pio device monitor --port PORT --baud 115200` or another serial terminal. Close the terminal before flashing or running the capture script.

```text
help
status
diag
settings
carriers
carriers results
wifi
wifi scan
ble
airplane status
airplane on
airplane off
apn NXTGENPHONE
light on
keyboard-light off
modem AT+CGDCONT?
modem AT+CEREG?
modem AT+COPS=?
modem restart
modem auto
inbox
scan-sim
keys on
keys off
home
```

The modem command accepts a small allowlist of read-only network queries. `modem inbox-me` explicitly dumps raw modem-store SMS PDUs, imports supported incoming messages, then restores the SIM read store; keep its output private. Reading may mark messages read and never deletes them. `modem restart` explicitly restarts the cellular module after a SIM change. `modem auto` requests automatic carrier selection. These operations refuse to interrupt active cellular work; automatic selection and an explicit `AT+COPS=?` scan can each take up to three minutes. Wait for completion before further cellular operations.

These commands perform real sends/call operations when the necessary hardware and service are available:

```text
sms +15551234567|Hello from Handheld OS
dial +15551234567
answer
hangup
lora Hello channel
```

Replace the example number with a consenting test recipient. `status` and `diag` report modem readiness, SIM state, LTE registration, signal, operator, radio status and persistence. Logs and NVS history may contain phone numbers and messages; review them before sharing.

## Project layout

```text
src/                   Board, e-paper apps, settings, modem, GPS, history and mesh services
lib/DeckCore/src/      Host-testable AT/SMS/AI/mesh/NMEA parsers, keyboard, layout and canvas
lib/GxEPD2/            Original GPL display-driver provenance and license
boards/               T-Deck Pro ESP32-S3 / 16 MB flash profile
config/settings.h     915 MHz radio settings and revision/modem defaults
tests/                Protocol and parser regression tests
tools/                Sanitized host test runner and USB bench logger
docs/                 Hardware, flashing, architecture and protocol notes
NEXT_STEPS.md          Local-machine handoff and hardware acceptance checks
```

Local overrides belong in ignored `config/local.h`; for example, `#define DECK_BOARD_REVISION 1` forces v1.1. Keep the defaults until the hardware profile is confirmed. Pins and reference revisions are recorded in [docs/HARDWARE.md](docs/HARDWARE.md).

GitHub Actions runs the host tests and firmware build and uploads build outputs. [docs/VALIDATION.md](docs/VALIDATION.md) records the software checks and their limits; hardware acceptance remains in [NEXT_STEPS.md](NEXT_STEPS.md).

Licensed under GNU GPL version 3 (`GPL-3.0-only`); see [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md). Third-party components retain their own licenses and notices.
