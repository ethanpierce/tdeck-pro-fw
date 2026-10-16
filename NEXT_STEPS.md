# Next hardware checks

Handheld OS is running on a T-Deck Pro v1.1. The original flash was backed up
and verified before upload. The launcher, USB console, physical shortcut mapping,
Settings app, light controls, saved APN and settings persistence passed bench
checks. Read the [dated bench record](docs/BENCH_2026-10-16.md) for the observed
results and the [flashing guide](docs/FLASHING.md) before another upload.

## Connectivity update

Settings now includes C for carrier scanning, W for Wi-Fi and B for Bluetooth
LE. The physical Wi-Fi/password workflow connected successfully and obtained a
DHCP address. BLE advertising was detected by the Mac. Keyboard and phone
pairing tests were deferred at the user's request; Bluetooth is left off.
Read [CONNECTIVITY.md](docs/CONNECTIVITY.md) for setup and test limits. Meshtastic
has a supported T-Deck Pro target and the board has flash headroom, but it has
not been integrated into Handheld OS.

## Cellular connection

An activated AT&T prepaid SIM registered at home on AT&T (`310410`), LTE Band 5,
after saving `NXTGENPHONE` and restarting the modem. Packet attachment and active
context 1 were confirmed. This demonstrates a connection at the test location;
it does not establish carrier approval, coverage elsewhere, or long-term service.
The earlier T-Mobile SIM did not register; its accepted APN could not resolve
the modem's limited overlap with US T-Mobile radio bands.

1. Keep the APN matched to the activated SIM's plan. AT&T consumer LTE phone
   service uses `NXTGENPHONE`; IoT/data plans may require another APN.
2. Query `modem AT+CEREG?`, `modem AT+CSQ`, `modem AT+COPS?` and
   `modem AT+CPSI?`. An explicit `modem AT+COPS=?` scan can take three minutes;
   allow it to complete before more cellular work.
   After a SIM change, use `modem restart`; `modem auto` requests automatic
   selection when idle. Neither command sends a message or places a call.
3. One consenting recipient confirmed receiving an outbound SMS. Their reply
   was not found: the SIM store was full (5/5) and modem memory empty (0/180).
   The new setup prefers ME reception, retaining all SIM messages, and imports
   both stores. After flashing, verify `modem AT+CPMS?` shows SM/SM/ME (or the
   reported SM fallback), then confirm a fresh incoming SMS, reset persistence
   and duplicate filtering. A full SIM is a plausible delivery blocker, not a
   confirmed explanation for the missing reply. Neither store is auto-deleted.
4. Test calls and microphone/speaker audio separately once carrier voice support
   is established. AT/SIM success alone does not prove VoLTE or working audio.

| CEREG state | Meaning |
| --- | --- |
| 0 / 2 / 4 | Not registered / searching / unknown |
| 1 / 5 | Registered home / roaming |
| 3 | Registration denied; investigate SIM/device/carrier eligibility |
| 6 / 7 | Registered for SMS only, home / roaming |
| 8 / 11 | Emergency-only; not ordinary messaging or call readiness |
| 9 / 10 | Registered home / roaming, CSFB not preferred; verify service |

## Device and radio

- Verify held Shift/Alt, Space, Backspace and text editing on the physical keyboard.
- Check every launcher/app transition visually and repeat boot on battery power.
- Use a second compatible 915 MHz SX1262 peer with matching
  [protocol settings](docs/PROTOCOL.md); confirm one short message each way and
  operation without a SIM. Reception and transmission remain untested.
- Measure charging, idle current, battery life and temperature before sleep work.
- Keep factory dumps, SIM contents, raw logs, camera images and local settings
  outside the public repository. Local backups are not replaced by a new build.

Future app work: contacts, timestamps, Unicode fonts, message deletion, additional touch gestures,
interoperable radio protocols, audio setup and measured power management.

## Conversations, touch and AI testing

Messages now has one SMS thread per numeric peer, one AI Assistant thread and a
shared Nearby LoRa broadcast group. Select with J/K and Enter or tap a row.
Type immediately in a thread; Enter or Send submits. Scroll with Older/Newer or
Shift + Alt + K/J. Ordinary N/J/K/C letters remain text. Verify the new list,
reply-address grouping, inline compose and back navigation on the device.
Touch is enabled only for a recognized CST328/CST3530 controller reporting
240×320; verify row selection, distinct Send hit area and stale-screen handling.

The earlier AI build passed browser setup, physical composition and a live
Wi-Fi provider request using OpenAI `gpt-5.6-luna`; the camera confirmed the
reply and LTE header. New hardware checks remain: the unified thread controls,
four model presets plus Custom, the 500-character limit, Wi-Fi's separate
three-bar header, and API-key retention after a restart. Model presets are a
fixed convenience list and do not establish provider/account access.

The AI transport now prefers connected Wi-Fi and otherwise uses LTE with
ESP32-verified TLS over the cellular task's TCP broker. Force an LTE test by
turning Wi-Fi off, then check that the thread reports LTE and a provider reply
returns. This LTE request has not yet passed on hardware. Keep the key on the
device and out of chat/logs. See [AI_CHAT.md](docs/AI_CHAT.md).

Public firmware uses [the browser installer](https://ethanpierce.github.io/tdeck-pro-fw/)
and separate simple [macOS](docs/INSTALL_MACOS.md) / [Windows](docs/INSTALL_WINDOWS.md)
guides. Actual Windows hardware flashing is unverified. The current LoRa protocol
still needs replacement/integration for the intended SMS plus Meshtastic messaging
device; a Meshtastic-compatible leaf implementation is a candidate, not installed
functionality.
