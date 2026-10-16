# Next hardware checks

Handheld OS is running on a T-Deck Pro v1.1. The original flash was backed up
and verified before upload. The launcher, USB console, physical shortcut mapping,
Settings app, light controls, saved APN and settings persistence passed bench
checks. Read the [dated bench record](docs/BENCH_2026-10-16.md) for the observed
results and the [flashing guide](docs/FLASHING.md) before another upload.

## Cellular connection

The A7682E responds and the SIM is ready, but normal network registration has
not succeeded. A local scan detected AT&T (`310410`) and FirstNet (`313100`),
with no T-Mobile entry. Later registration was denied. `fast.t-mobile.com` was
saved and accepted by the modem; that does not resolve RF or carrier eligibility.

1. Check the antenna, intended location, SIM service and the exact modem/carrier
   compatibility. The A7682E lacks most US T-Mobile bands; firmware cannot add them.
2. Query `modem AT+CEREG?`, `modem AT+CSQ`, `modem AT+COPS?` and
   `modem AT+CPSI?`. An explicit `modem AT+COPS=?` scan can take three minutes;
   allow it to complete before more cellular work.
3. Proceed to consented two-way SMS only after an appropriate registration
   state. Test reception, submission, reset persistence and duplicate filtering.
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

Future app work: contacts, timestamps, Unicode fonts, message deletion, touch,
interoperable radio protocols, audio setup and measured power management.
