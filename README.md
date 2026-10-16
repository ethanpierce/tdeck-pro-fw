# Handheld OS

A keyboard-first firmware foundation for the **LILYGO T-Deck Pro with an A7682E modem and 915 MHz SX1262 radio**. It runs on the ESP32-S3 using Arduino/FreeRTOS, with an e-paper home screen, SMS, call controls, LoRa chat, and USB diagnostics.

An independent hobby project. Hardware and carrier names identify compatible equipment and services; they do not imply affiliation or endorsement.

**The A7682E is a poor match for T-Mobile in the United States.** SIMCom specifies LTE bands 1/3/5/7/8/20; it lacks T-Mobile's B2/B4/B12/B66/B71. B5 provides only limited possible overlap. Firmware and APN changes cannot add missing radio bands. LTE registration, SMS service, and especially VoLTE must be checked on the actual device before treating it as a phone. See [the hardware notes](docs/HARDWARE.md).

This is a starter for hardware bring-up, with a firmware build and host-side protocol tests. **It was built, flashed and bench-tested on a v1.1 device on 2026-10-16.** E-paper, keyboard shortcuts, the Settings app, USB, modem/SIM response and LoRa initialization worked. The APN and light settings persisted across a restart. The network scan found AT&T and FirstNet, but no T-Mobile entry; normal cellular registration did not succeed. See the [bench record](docs/BENCH_2026-10-16.md) for evidence and untested functions. Call audio and carrier voice support remain unverified. Continue with [NEXT_STEPS.md](NEXT_STEPS.md).

## Included

| Feature | Starter behavior |
| --- | --- |
| Home screen | E-paper launcher, battery reading, registration and radio status |
| SMS | Compose/send a single GSM-7 message; import supported messages from SIM storage |
| Phone | Dial, answer, hang up, incoming-call indication, modem call-state polling |
| LoRa | Direct plaintext broadcasts between compatible Handheld OS peers at 915 MHz |
| History | Last 16 combined SMS/LoRa messages in NVS, with integrity checks and duplicate filtering |
| Settings | On-device APN editor, screen/keyboard light toggles, saved preferences and status shortcut |
| Diagnostics | USB commands, SIM/registration/signal/modem queries, keyboard tracing and log capture |

LoRa uses a small [custom packet format](docs/PROTOCOL.md). It does not interoperate with Meshtastic or LoRaWAN and has no mesh routing, encryption, delivery acknowledgement, or automatic retransmission. A second compatible radio is needed to test messaging. No SMS, call, or LoRa payload is sent automatically at startup; the cellular modem is powered and attempts network registration.

Touch input, contacts, Wi-Fi apps, browser, GPS, deep sleep, audio routing and a full smartphone application framework are future work. This project targets T-Deck Pro v1.0/v1.1; T-Deck Pro MAX is unsupported.

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

## Keyboard

| Screen | Keys |
| --- | --- |
| Home | `M` Messages, `P` Phone, `L` LoRa, `D` diagnostics, `S` Settings; use lowercase/unshifted keys |
| Any screen | `Sym` returns home and cancels the current draft |
| Messages / LoRa | `N` compose, `J` next page, `K` previous page; hold `Alt` and press printed `1`–`4` to open a message |
| Number entry | `W/E/R` → `1/2/3`, `S/D/F` → `4/5/6`, `Z/X/C` → `7/8/9`, `P` → `0`, `O` → `+`; no Alt needed |
| SMS compose | Enter after the number opens the text field; Enter in the text field sends |
| LoRa compose | Enter sends the broadcast |
| Phone | Enter dials the entered number; `A` answers and `H` hangs up while a call is present |
| Diagnostics | `R` reconnects/refreshes the modem; `I` imports the SIM inbox |
| Settings | `A` edit APN, `F` toggle screen light (v1.1), `K` toggle keyboard light, `D` status, `R` refresh modem |
| APN editor | Enter saves and applies the APN; `Sym` cancels |
| Message detail | Any key returns to the inbox |

Hold `Shift` for uppercase and `Alt` for the printed alternate symbols when composing. The microphone key is not a voice-input feature. The e-paper display coalesces typing and refreshes slowly; USB output provides a quicker view while debugging.

Outbound SMS accepts printable ASCII characters available in GSM-7, up to 160 septets; extension characters such as `{`, `}` and `\` each consume two. Incoming single-part GSM-7 and UCS-2 messages from numeric senders are supported. Multipart messages, alphanumeric senders and other encodings remain on the SIM. UTF-8 text is retained in history and USB output; unsupported e-paper font characters appear as `?`.

“Modem accepted” confirms an SMS submission, not delivery to the recipient. “Transmitted” confirms the LoRa radio completed transmission, not reception by another device. An interrupted operation may become “Result unknown”; check the receiving device before manually retrying. SIM messages are never automatically deleted.

## Settings app

Press `S` on Home. Light changes save automatically in NVS; the front light is available on v1.1. The APN editor accepts 1–63 letters, digits, dots or hyphens. It suggests `fast.t-mobile.com` when no APN is saved; nothing is applied until Enter is pressed. A saved APN is applied as context 1 with `IPV4V6` when the modem is ready, and restored at boot. The firmware does not start a mobile-data session. A working APN cannot add radio bands or establish carrier eligibility.

## USB console

Open at 115200 baud with newline endings, using `pio device monitor --port PORT --baud 115200` or another serial terminal. Close the terminal before flashing or running the capture script.

```text
help
status
diag
settings
apn fast.t-mobile.com
light on
keyboard-light off
modem AT+CGDCONT?
modem AT+CEREG?
modem AT+COPS=?
inbox
scan-sim
keys on
keys off
home
```

The modem command accepts a small allowlist of read-only network queries. An explicit `AT+COPS=?` scan can take up to three minutes and occupies the modem task while it runs; wait for the result before further cellular operations.

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
