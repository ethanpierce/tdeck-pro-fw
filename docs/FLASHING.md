# Build, back up and flash

Target: ESP32-S3 T-Deck Pro v1.0/v1.1 with 16 MiB flash, 8 MiB QSPI PSRAM, A7682E and 915 MHz SX1262. T-Deck Pro MAX has a different hardware profile and is unsupported. The [2026-10-16 bench record](BENCH_2026-10-16.md) documents a verified original backup, successful upload, working hardware checks and remaining limitations.

## Set up the local machine

Use Python 3.12, Git and internet access for the first toolchain install. On Linux/macOS:

```sh
cd t-deck-pro-os
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
python -m pip install esptool==4.5.1
```

On Windows PowerShell:

```powershell
cd t-deck-pro-os
py -3.12 -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements-dev.txt
python -m pip install esptool==4.5.1
```

PlatformIO and pyserial are pinned in `requirements-dev.txt`. The separate esptool install makes the backup/restore commands below available as `python -m esptool`. The firmware build also obtains its own compatible esptool through PlatformIO.

Install `g++` or `clang++` to run `bash tools/test.sh` on Linux/macOS/WSL. The tests run with AddressSanitizer and UndefinedBehaviorSanitizer; they do not need the device. Build the firmware on any supported PlatformIO host with:

```sh
pio run -e tdeck-pro
```

## Find the USB port and enter download mode

1. Connect the charged device and battery with a USB **data** cable.
2. Run `python tools/bench.py --list` or `pio device list`.
3. If the normal upload cannot connect, use the vendor's download sequence: hold the top/BOOT half of the right-side rocker, press and release the lower/RST half, then release BOOT. Identify the controls on the actual revision before pressing them.
4. List ports again. Download mode and the running firmware may have different port names. Typical examples are `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem...` on macOS and `COM5` on Windows.

Use that port wherever `PORT` appears below. Close all serial monitors before opening the port with another tool. Linux may require adding your local user to the system's serial-access group; use the distribution's normal device permissions.

## Save the factory image first

Create a `backups` directory outside the repository and run this command from that directory. The last argument is the output filename:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 115200 read_flash 0x0 0x1000000 factory-16mb.bin
```

Verify the file is exactly **16,777,216 bytes** and keep a second copy. To verify the size and record a SHA-256 on any host:

```sh
python -c "from pathlib import Path; import hashlib; p=Path('factory-16mb.bin'); print(p.stat().st_size, hashlib.sha256(p.read_bytes()).hexdigest())"
```

This backup is the ESP32 flash, including its firmware and stored data. It does not copy or update the A7682E's internal modem firmware or the SIM. Factory dumps may contain private data and should stay out of Git.

## Upload the starter

Return to the repository, with its Python environment active:

```sh
pio run -e tdeck-pro -t upload --upload-port PORT
```

PlatformIO handles the bootloader, partition table, boot app metadata and application offsets. This project uses its supplied partition table; the first upload replaces the factory application layout. No separate `erase_flash` step is required. The project does not update modem firmware, SIM settings or fuel-gauge calibration.

If automatic restart does not happen, press and release RST without holding BOOT. Give the e-paper panel and modem several seconds to initialize. Re-list the USB port.

```sh
python tools/bench.py --port PORT --seconds 60
```

Point the camera at the complete screen and keyboard. The capture script requests `status` after one second, `diag` after three seconds and another `status` after 35 seconds. It records text under ignored `logs/` unless `--output PATH` is given. If nothing is captured, verify that the port belongs to the running firmware and the device has been reset out of download mode.

For manual use:

```sh
pio device monitor --port PORT --baud 115200
```

Use newline line endings and type `help`, `status` or `diag`. Close this monitor before running the bench capture script.

## Build outputs

PlatformIO writes these files under `.pio/build/tdeck-pro/`:

| File | Purpose |
| --- | --- |
| `firmware.bin` | Application image; normally flashed at `0x10000` |
| `firmware.elf` | Symbols for crash decoding and debugging |
| `bootloader.bin` | ESP32-S3 bootloader at `0x0` |
| `partitions.bin` | This project's partition table at `0x8000` |

Use the PlatformIO upload command for a source build. Flashing the application alone over unknown factory partitions is insufficient.

## Use the supplied prebuilt image

The starter ZIP includes an ignored `firmware/` directory inside its Git working copy, containing `combined.bin`, individual images, `firmware.elf`, `BUILD_INFO.txt` and `SHA256SUMS`. Read the build information to match the image to its source commit. The combined image includes the bootloader at `0x0`, partitions at `0x8000`, `boot_app0.bin` at `0xE000` and the application at `0x10000`.

With the original flash backup complete, enter download mode and flash the **combined** image at address `0x0`:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 115200 write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x0 firmware/combined.bin
```

Reset normally, re-list the port and capture first boot as described above. The prebuilt firmware has the same hardware validation limits as a local source build.

To create a fresh handoff after later source changes, commit them, run `pio run -e tdeck-pro`, then `python tools/package.py`. The packaging tool requires a clean Git working tree and runs the pinned PlatformIO project build again to validate the images against the source. Release packaging rejects ignored `config/local.h` overrides; move intended release settings into tracked `config/settings.h` and commit them first. It produces a ZIP with a Git checkout, prebuilt images and checksums, and a separate Git bundle next to the repository. Output names may be changed with `--output` and `--bundle`.

## Restore the original device

Enter download mode, close other port users and use the saved full-size dump:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 115200 write_flash 0x0 factory-16mb.bin
```

Reset normally afterward. A full restore replaces the ESP32's current applications, partitions and data with the backup. It does not restore a modem configuration changed outside this project.

## Quick troubleshooting

| Symptom | Check |
| --- | --- |
| No USB device | Data-capable cable, USB socket, download-mode sequence, host USB permissions |
| esptool cannot connect | Correct download-mode port, BOOT/RST sequence, closed serial monitor |
| Upload succeeds but no log | Reset out of download mode; check changed CDC port and 115200 baud |
| E-paper frozen/old screen | Serial logs first; verify v1.0/v1.1 detection and panel reset wiring |
| No modem AT response | Battery and charge, modem supply, UART mapping and power-key timing |
| Keyboard not responding | Use USB commands and `keys on`; check TCA8418 detection and event mapping |
| LTE searching indefinitely | Compare A7682E bands with local service; see [HARDWARE.md](HARDWARE.md) |

The v1.0 display has no independent reset line. If display initialization remains stuck even with the correct build, consult the vendor's e-paper recovery guidance before repeatedly changing firmware.
