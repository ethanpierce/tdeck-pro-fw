# Development paused — 2026-10-16

Paused at the user's explicit request before a flight. All three subagents have
been stopped. Do not resume development, radio testing, sending, flashing,
release publication or pairing until the user asks to continue. The local USB
console and browser setup server were closed. No automatic follow-up is set.

This is a **work-in-progress source checkpoint**, not a tested release. The
combined UI/Meshtastic/GPS/Airplane build has not been flashed or fully validated.
The new Airplane mode is therefore **not installed on the bench device**.

## State of the work

- Native ESP-IDF 5.4.0 / FreeRTOS migration is implemented. No Arduino framework,
  Adafruit runtime wrappers or NimBLE-Arduino are linked. RadioLib 7.1.2 uses a
  native HAL. Native board GPIO mux, shared SPI and modem warm-TCP fixes are in
  source. The flash partition table and existing NVS layouts are preserved.
- Safe display improvements are implemented and were exercised in the native
  baseline: removed the old 1,200 ms gate, composer-region updates, 100 ms typing
  quiet / 250 ms burst bound, captured-key timestamps and fail-closed overflow.
  Stock waveforms, 4 MHz SPI and periodic cleaning remain unchanged.
- UI agent completed and froze a minimal Home with Messages / Phone / Settings,
  touch numeric dialer, unified conversations, categorized touch settings,
  Mesh setup, GPS status and location-SMS drafts. Full phone numbers remain
  visible. Composer-only frames avoid rebuilding history. History layout is
  bounded to 300 rows, including hostile newline-heavy input. New UI geometry
  and render fixtures passed focused sanitized tests and native syntax checks.
- Meshtastic agent implemented a native channel-text leaf client, encrypted
  Data/User packets, 16 heard nodes, 64 duplicate entries, explicit node
  announcement and bounded CAD/backoff. No relaying, PKI direct messages,
  automatic announcements, MQTT or delivery receipts. Mesh defaults OFF and
  region unset. Upstream firmware/protobuf pins and independent fixtures are
  in [MESH.md](MESH.md); a physical peer test has not occurred. Agent was stopped
  before its final combined validation/freeze report.
- GPS agent implemented receive-only UART2, power control, bounded validated
  NMEA, fix/UTC/satellite/accuracy status, stale-fix rejection and Google Maps
  links for explicit SMS drafts. Default OFF; Airplane mode suspends it. Latest
  edits expire stale UTC and exclude estimated/manual/simulated fixes from
  satellite-lock sharing. Those final edits still need the full repeat run.
  No live satellite lock or actual location-SMS send has been tested.
- Airplane coordinator and persistence are implemented. Cellular power-off
  bypasses the action queue, startup waits are interruptible and rapid toggle
  generations invalidate queued work. Wi-Fi/BLE/Mesh/GPS have suspension gates;
  actual shutdown acknowledgements determine status. AI requests have generation
  guards. Existing radio preferences survive suspension. Corrupt saved policy
  fails closed, and shutdown applies even if saving fails. Integrated behavior
  and power measurements remain untested.
- New radio-policy and Mesh blobs are separate from existing APN/lights,
  wireless credentials, message history and AI settings. No settings erase or
  SIM-message deletion was performed.

## Last deployed and verified baseline

The device still has the optimized native image **before the new UI/Mesh/GPS/
Airplane changes**:

- Application image: **1,413,472 bytes**.
- SHA-256: `2a45c4215079ea0b98a3cd685817ea48fc5e7dc4b574cc3382513174e9feb550`.
- Static RAM: **52,488 bytes**; app slot is 6 MiB, flash 16 MiB, PSRAM 8 MiB.
- Partial display frames approximately 729–731 ms; clean refresh ~1,111 ms.
- Minimum observed internal heap 118,899 bytes with Wi-Fi initialized; PSRAM
  free ~8.38 MB. One dropped cellular UI event was observed at startup, then
  stayed unchanged; physical dropped-key count stayed zero.
- User confirmed touch, responsive keys and complete rapid typed input.
- Wi-Fi HTTPS AI, cold/warm LTE AI and Wi-Fi → OFF → LTE AI passed using the
  saved provider configuration. TLS verification remains required.
- User-initiated AT&T B5 calls reached Active; user confirmed a call worked.
  The assistant did not initiate those calls. Individual audio routes remain
  unmeasured.
- Incoming SMS remains unresolved: SIM 5/5, ME 0/180; SM/SM/ME, PDU mode, CNMI
  and all-interface URCs verified. Fresh replies were not found in either store.
  Preserve carrier messages. An earlier outbound SMS was confirmed delivered.
- BLE pairing remains deferred by the user; leave BLE OFF. Current Wi-Fi
  preference is OFF, with credentials retained. Mesh/GPS were not enabled.

## Resume sequence and software tests

1. Read this checkpoint, [ARCHITECTURE.md](ARCHITECTURE.md),
   [VALIDATION.md](VALIDATION.md), [MESH.md](MESH.md), [GPS.md](GPS.md) and
   the [dated bench record](BENCH_2026-10-16.md). Check Git status before edits.
2. Finish review of the interrupted Mesh/GPS work and integration. In particular:
   inspect radio shutdown/restart/initialization errors, no stale queued work,
   all saved preferences captured under boot suspension, failure acknowledgements,
   AI generation guards, modem cancellation and Mesh CAD/timeout cleanup.
3. `tools/test.sh` currently includes fifteen binaries. **Add the completed
   UI layout suite to that runner** using `tests/ui_layout_test.cpp`,
   `lib/DeckCore/src/ui_layout.cpp` and `lib/DeckCore/src/mono_canvas.cpp`, then
   run the full suite with ASan/UBSan and warnings as errors. Check GCC as well
   as macOS Clang for misleading-indentation warnings.
4. Run the standalone Mesh official-schema fixture generator/verification per
   [MESH.md](MESH.md), and re-run NMEA/GPS lifecycle tests after the latest UTC
   and real-fix filters. Native AES128/AES256 self-test must pass before RF.
5. Earlier native baseline passed all eleven original suites. Parent's focused
   modem suite passed 33 scenarios including Airplane boot, in-flight SMS abort
   without replay and rapid-toggle queued-call cancellation. Separate settings
   persistence tests passed before the final zero-length-policy guard edit.
   These results do not replace a full test of this checkpoint.
6. Use **the repo's `.venv/bin/pio` and `.venv/bin/python`**. The system PATH pio
   is an x86 Python installation and can replace downloaded tool packages with
   the wrong architecture on this Mac. Do not run shared builds/tests concurrently.

```sh
bash tools/test.sh
.venv/bin/pio run -e tdeck-pro
.venv/bin/python tools/release.py --self-test
```

7. Record actual application bytes and static RAM for the combined build, compare
   with the baseline above, and inspect warnings. Keep the 6 MiB app slot and
   unchanged NVS gap. Recheck `-O2`, assertions, watchdogs, verified TLS, CPU
   affinity and task stacks. Update VALIDATION.md's suite count/results only
   after the combined pass; its eleven-suite section currently describes the
   earlier native baseline.
8. Review source and documentation consistency, local links and privacy before
   any public push/package. No bundled keys or credentials. No release should
   be labelled tested based only on the individual agent reports.

## Hardware acceptance after the software pass

- Reconnect the device and BRIO when the user is ready. Confirm there is no
  active user call before touching cellular state. After the user resumes
  development, continue the already-authorized bench work; do not initiate new
  SMS messages or calls without authorization.
- Back up current NVS privately, without reading/extracting its stored API key.
  Existing original flash and pre-native NVS backups must not be overwritten.
- Flash the four separate native images with the known-working ROM
  `--no-stub` path from [FLASHING.md](FLASHING.md). No erase-all, padded merged
  image or NVS overwrite. Close serial/WebSerial before upload; check hashes.
- Check native startup, saved lights/APN/key/network/history, all touch and
  keyboard app flows, full phone-number display, message-list selection and
  history scrolling. Do not dial or send an SMS merely to test a target.
- Airplane ON: verify cellular rail, Wi-Fi driver, BLE host/controller, LoRa and
  GPS are off; no scans/sends/restarts. Restart while ON and confirm no brief
  radio startup. OFF restores only prior enabled preferences and never replays
  messages/calls/AI requests. Test quick toggles and mid-operation cancellation.
  Do not claim measured RF suppression/current without actual measurements.
- GPS: user enables it outdoors, wait for a real fresh fix, compare coordinates,
  satellite/time/accuracy status and stale/lost-fix handling. Share by SMS should
  select a recipient, show a captured Google Maps link/time and require Send.
  A location draft is not permission for the assistant to send it.
- Mesh: explicitly select the correct supported US region on suitable hardware,
  match a stock peer's preset/channel/key, test text both ways and heard nodes,
  clear-channel/busy/timeouts, duplicate suppression and no-SIM operation. Public
  default key has no privacy. No physical peer is currently confirmed available.
- Repeat benign AI tests over Wi-Fi and LTE after the user resumes development.
  Keep the key on device; confirm cancelled requests never auto-resume.
- Investigate inbound SMS without deleting SIM/ME data. New test SMS/call needs
  user authorization/consenting recipient. BLE pairing stays deferred unless
  requested again.
- Measure peak/minimum internal heap and main/worker stack headroom with full
  history, AI traffic and repeated radio cycles; verify no leak, key loss or
  starvation during e-paper refresh. Battery/current/cold-power and Windows
  hardware installer acceptance remain outstanding.

## GitHub release work still pending

The public repository is GPL-3.0-only with the generic name Handheld OS. The
latest source checkpoint is not a release. No `v0.1.0` release is published and
GitHub Pages is not enabled. Easy macOS/Windows browser-installer packaging is
prepared but requires the combined validation and exact-source rebuild.

After acceptance: commit final source, run CI, rebuild/package from an exact
clean public commit, validate source/licenses/four-part images/OTA metadata/
checksums and NVS exclusions, test that exact image, publish a clearly labelled
prerelease and enable/verify the hosted installer. Do not push all branches:
the original private starter history exists on a local-only preservation branch.
Do not package local build output or backups instead of the clean release.

Ignored local portable release/tool caches and prepared release notes exist;
recreate their source clone from the final public commit and update notes for
actual tested scope. See `tools/release.py` and the install guides. Do not claim
Windows hardware flashing or Mesh interoperability until physically tested.
