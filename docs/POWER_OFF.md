# Power off and BOOT hold

In Handheld OS, hold the physical **BOOT** button continuously:

- **5 seconds:** start a clean shutdown. A **Shutting down** screen appears
  while radio services stop and final message results are saved.
- Release BOOT after shutdown starts. Once cleanup finishes, the device shows
  **Powered off — Press RESET to start again** and enters deep sleep.
- **30 seconds total, without releasing:** force shutdown through a separate
  core-0 task, even if the UI task is blocked. The forced path bypasses display,
  storage, I2C and SPI work, disables/holds the external radio rails and enters
  deep sleep. A forced shutdown cannot guarantee an updated e-paper image or
  the last unsaved state. It never retries interrupted messages.

Release BOOT before pressing **RESET** to start Handheld OS again. Holding
BOOT while resetting retains its normal ESP32 download-mode behavior. BOOT is
not a wake source after shutdown; releasing it does not restart the OS.

Settings also has a **Power off** button at the top right. Confirm Power off
or choose Cancel. The keyboard shortcut in Settings is **O**. The explicit USB
command `power off` uses the same clean shutdown flow.

## Hardware limits

This is **radio-off deep sleep**, not physical battery isolation. It is designed for
USB and battery operation without unplugging the battery, while retaining
RESET-to-start. USB shutdown passed. The user confirmed both physical BOOT
hold paths, the shutdown screens and RESET wake-up. Battery-only operation
and residual current must still be measured on the board.

The [v1.1 schematic](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/blob/HD-V2-250915/hardware/T-Deckpro%20v1.1%2025-09-15/H693%20H779%20T-DeckPro%20V1.1%2025-09-15.pdf)
connects BOOT to GPIO0 and RESET to the ESP32 reset input. The charger's QON
power-on input is not connected to either button. Battery ship mode would
therefore remove the power needed for RESET to restart the MCU and USB could
still power the system. The implementation deliberately does not change the
charger's BATFET, charge limits or battery calibration. See [TI's BQ25896
shipping-mode description](https://www.ti.com/lit/ds/symlink/bq25896.pdf) and
[ESP-IDF deep-sleep documentation](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html).

The 30-second path is independent of the UI task; it is not an electrical
hardware failsafe against a completely halted MCU or disabled interrupts.

## Implementation and validation

A 4,096-byte task stack on core 0 monitors GPIO0 every 20 ms. The portable
button state machine debounces 40 ms, requires a stable initial release after
boot, emits each threshold once and handles the millisecond counter wrapping.
Holding BOOT during reset/flashing cannot turn into an OS shutdown gesture.

Clean shutdown stops new input/provider requests, suspends radios without
changing saved enable/Airplane preferences, drains final cellular events and
marks remaining pending history entries unknown. Settings/history writes are
synchronous commits. The UART owner tries AT+CPOF only when idle; a busy SMS/TCP
exchange is cancelled without injecting a command into its payload. Modem
power-off waits are bounded. Lights and keypad scanning stop, the e-paper driver
powers down after its final frame, external power GPIOs are held disabled and
all configured sleep wake sources are removed. RESET releases held outputs
through normal board startup and restores the user's saved preferences.

The force path performs no NVS, I2C, SPI or display operation. Pending persisted
messages already become unknown at the next boot, preserving the no-replay
behavior even when clean shutdown cannot finish.

Host validation covers exact thresholds, contact bounce, startup-held BOOT,
release before a threshold, clock wrapping, Settings/USB shutdown, cleanup while
BOOT stays held, actual monitor operation with no UI progress, and the ordered
rail-hold/reset-only sleep boundary. Cellular tests cover idle modem power-off,
no-response deadline, shutdown during SMS with unknown result/no extra send,
and shutdown before modem startup. The user confirmed the 5-second and
continuous 30-second BOOT tests with RESET wake-up. The final corrected image
also passed boot and clean USB shutdown. Battery-only behavior and off-state
current remain unmeasured. Exact images and results are in the
[bench log](BENCH_2026-10-16.md).
