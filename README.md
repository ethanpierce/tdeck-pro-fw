# Handheld OS

A keyboard-first firmware foundation for the **LILYGO T-Deck Pro with an A7682E modem and 915 MHz SX1262 radio**. It runs on the ESP32-S3 using Arduino/FreeRTOS, with e-paper conversations for SMS, AI and nearby LoRa broadcasts, optional touch controls, call controls and USB diagnostics.

**[Install in your browser](https://ethanpierce.github.io/tdeck-pro-fw/)** — no build tools required.
[macOS instructions](docs/INSTALL_MACOS.md) · [Windows instructions](docs/INSTALL_WINDOWS.md) · [Firmware downloads](https://github.com/ethanpierce/tdeck-pro-fw/releases)

An independent hobby project. Hardware and carrier names identify compatible equipment and services; they do not imply affiliation or endorsement.

**The A7682E is a poor match for T-Mobile in the United States.** SIMCom specifies LTE bands 1/3/5/7/8/20; it lacks T-Mobile's B2/B4/B12/B66/B71. B5 provides only limited possible overlap. Firmware and APN changes cannot add missing radio bands. LTE registration, SMS service, and especially VoLTE must be checked on the actual device before treating it as a phone. See [the hardware notes](docs/HARDWARE.md).

This is a starter for hardware bring-up, with a firmware build and host-side protocol tests. **It was built, flashed and bench-tested on a v1.1 device on 2026-10-16.** E-paper, keyboard shortcuts, the Settings app, USB, modem/SIM response and LoRa initialization worked. The APN and light settings persisted across a restart. Wi-Fi setup joined a real network, and a Mac detected the BLE companion service; keyboard/phone pairing tests were deferred. An activated AT&T prepaid SIM subsequently registered on LTE Band 5 with `NXTGENPHONE`, and packet attachment and context 1 activation were confirmed. The earlier T-Mobile SIM did not register. See the [bench record](docs/BENCH_2026-10-16.md) for evidence and untested functions. Call audio and carrier voice support remain unverified. Continue with [NEXT_STEPS.md](NEXT_STEPS.md).

## Included

| Feature | Starter behavior |
| --- | --- |
| Home screen | E-paper launcher, battery reading, registration and radio status |
| Status bar | Wi-Fi state with three bars, cellular technology with four bars, and battery percentage |
| Conversations | One SMS thread per numeric peer, one AI thread, and one shared Nearby LoRa broadcast thread; inline composer |
| SMS | Compose/send a single GSM-7 message; prefer modem memory for reception, with SIM fallback; import supported messages from both stores |
| AI Chat | HTTPS over connected Wi-Fi or otherwise LTE; four model presets plus Custom; replies limited to 500 characters; LTE hardware validation pending |
| Touch | Cards, conversation rows, Send/history controls and model selection when a supported CST328/CST3530 240×320 controller is detected; hardware validation pending |
| Phone | Dial, answer, hang up, incoming-call indication, modem call-state polling |
| LoRa | Direct plaintext broadcasts between compatible Handheld OS peers at 915 MHz |
| History | Last 16 combined SMS/LoRa messages in NVS, with integrity checks and duplicate filtering |
| Settings | APN editor, cellular-carrier scan, Wi-Fi/BLE setup, light toggles and saved preferences |
| Wi-Fi | 2.4 GHz scan, network/password entry, DHCP connection, saved credentials and forget |
| Bluetooth LE | Keyboard discovery/HID host, authenticated phone companion service and bond controls; pairing bench tests deferred |
| Diagnostics | USB commands, SIM/registration/signal/modem queries, keyboard tracing and log capture |

LoRa uses a small [custom packet format](docs/PROTOCOL.md). It does not interoperate with Meshtastic or LoRaWAN and has no mesh routing, encryption, delivery acknowledgement, or automatic retransmission. A second compatible radio is needed to test messaging. No SMS, call, or LoRa payload is sent automatically at startup; the cellular modem is powered and attempts network registration.

Contacts, a browser, GPS, deep sleep, audio routing and a full smartphone application framework are future work. This project targets T-Deck Pro v1.0/v1.1; T-Deck Pro MAX is unsupported.

## Build and flash

Use Python 3.12, Git and a C++ compiler (`g++` or `clang++`) for the host tests. The first firmware build downloads the pinned PlatformIO tools and libraries.

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

Press **M** from Home for the conversation list. Choose **New message**, **AI Assistant**, **Nearby LoRa**, or a phone-number thread with J/K and Enter, or tap its row when touch is available. New message asks for a phone number; Enter or Continue opens its thread without sending. SMS threads group the same numeric peer with or without a leading `+`, retaining the original reply address. There is no contact address book or unread count yet. Nearby LoRa is one shared broadcast group, not private radio messaging.

Inside a thread, **type immediately** into the composer and press **Enter** or tap **Send** to send. Ordinary N/J/K/C letters are message text. Use **Older/Newer** or **Shift + Alt + K/J** to scroll history. **Sym** or the top-left **<** goes back to the list or Home, according to where the thread was opened. E-paper refresh is slow; touches against a stale screen are ignored until redraw completes.

| Screen | Keys |
| --- | --- |
| Home | `M` Messages, `P` Phone, `L` LoRa, `D` diagnostics, `A` AI Chat, `S` Settings; use lowercase/unshifted keys |
| Navigation | `Sym` goes back; leaving a thread cancels its draft, while an already submitted request continues |
| Conversation list | `J/K` select a row; Enter opens it; touch selects the displayed row |
| SMS / AI / Nearby LoRa thread | Type into the composer; Enter or Send submits; Shift + Alt + K/J scrolls older/newer history |
| Number entry | `W/E/R` → `1/2/3`, `S/D/F` → `4/5/6`, `Z/X/C` → `7/8/9`, `P` → `0`, `O` → `+`; no Alt needed |
| New SMS | Enter or Continue after the number opens its thread; nothing is sent until the message is submitted |
| AI model picker | Settings I → M; J/K and Enter, or tap one of four presets / Custom |
| Phone | Enter dials the entered number; `A` answers and `H` hangs up while a call is present |
| Diagnostics | `R` reconnects/refreshes the modem; `I` imports both SMS stores |
| Settings | `A` APN, `I` AI setup, `C` carrier scan, `W` Wi-Fi, `B` BLE, `F/K` lights, `D` status, `R` modem refresh |
| Carrier scan | `R` rescan, `J/K` results pages; up to three minutes |
| Wi-Fi | `W` radio toggle, `R` scan, `J/K` select, Enter connect, `N` hidden SSID, `F` forget |
| BLE | `B` radio toggle, `P` phone visibility, `R` discovery, `J/K` select, Enter keyboard pairing, `C` reconnect, `F/X` forget |
| APN editor | Enter saves and applies the APN; `Sym` cancels |

Hold `Shift` for uppercase and `Alt` for the printed alternate symbols when composing. The microphone key is not a voice-input feature. The e-paper display coalesces typing and refreshes slowly; USB output provides a quicker view while debugging.

Outbound SMS accepts printable ASCII characters available in GSM-7, up to 160 septets; extension characters such as `{`, `}` and `\` each consume two. Incoming single-part GSM-7 and UCS-2 messages from numeric senders are supported. Multipart messages, alphanumeric senders and other encodings remain in their original SIM or modem store. New reception prefers modem (ME) storage so a full SIM does not consume all receiving space; setup falls back to SIM storage if ME reception is unavailable. Both stores are scanned at boot and by the inbox import control. UTF-8 text is retained in history and USB output; unsupported e-paper font characters appear as `?`.

“Modem accepted” confirms an SMS submission, not delivery to the recipient. “Transmitted” confirms the LoRa radio completed transmission, not reception by another device. An interrupted operation may become “Result unknown”; check the receiving device before manually retrying. Stored SMS messages are never automatically deleted; a full receiving store can block new delivery.

## Settings app

Press `S` on Home. Light changes save automatically in NVS; the front light is available on v1.1. The APN editor accepts 1–63 letters, digits, dots or hyphens. It suggests `fast.t-mobile.com` when no APN is saved; nothing is applied until Enter is pressed. A saved APN is applied as context 1 with `IPV4V6` when the modem is ready, and restored at boot. An explicit AI request can open LTE packet data; saving setup or starting the device does not send a provider request. A working APN cannot add radio bands or establish carrier eligibility.

See [connectivity setup](docs/CONNECTIVITY.md) for the carrier scan, Wi-Fi credentials, BLE keyboard support, phone GATT service and pairing controls. The ESP32-S3 supports Bluetooth LE; Bluetooth Classic keyboards are incompatible. Wi-Fi and BLE start disabled until explicitly enabled.

Open **AI Assistant** in Messages, press **A** from Home, or use **S → I** for its endpoint, model and masked API key. In the AI thread, **...** opens setup. The four fixed model presets are GPT-5.6 Luna, Terra, Sol and GPT-5.4 Mini; **Custom** accepts another provider model ID. A preset does not establish account access. The default is OpenAI `gpt-5.6-luna`; other providers must implement HTTPS Chat Completions.

AI prefers connected Wi-Fi and otherwise uses ready LTE service. Turn Wi-Fi off in Settings W to explicitly test LTE. The thread reports the connection, key presence and active transport. Both paths verify HTTPS certificates and hostname; the LTE path runs TLS on the ESP32 over a modem TCP socket. Failed requests are not automatically retried on another connection. The key stays in device settings and conversations stay in RAM. Browser key setup, physical composition and a live Wi-Fi provider reply passed on the earlier interface; the new conversation/touch UI, LTE path and 500-character enforcement still need final hardware checks. See [AI setup and controls](docs/AI_CHAT.md).

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
lora Hello nearby handheld
```

Replace the example number with a consenting test recipient. `status` and `diag` report modem readiness, SIM state, LTE registration, signal, operator, radio status and persistence. Logs and NVS history may contain phone numbers and messages; review them before sharing.

## Project layout

```text
src/                   Board, e-paper apps, settings, modem, history and LoRa services
lib/DeckCore/src/      Host-testable AT parser, SMS codec, keyboard and radio protocol
lib/GxEPD2/            Pinned minimal vendor e-paper driver, including its license
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
