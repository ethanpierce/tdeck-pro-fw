# Install, back up and build

Target: ESP32-S3 T-Deck Pro v1.0/v1.1 with 16 MiB flash, 8 MiB QSPI PSRAM, A7682E and 915 MHz SX1262. T-Deck Pro MAX has a different hardware profile and is unsupported. The [2026-10-16 bench record](BENCH_2026-10-16.md) documents a verified original backup, successful upload, working hardware checks and remaining limitations.

## Easy installation: macOS and Windows

Open **[the Handheld OS installer](https://ethanpierce.github.io/tdeck-pro-fw/)** in desktop **Chrome or Edge**, connect the device with a USB data cable and leave its battery connected. Click **Connect T-Deck Pro**, choose the Espressif USB serial device, then choose **Install**. Leave **Erase device unchecked**, click **Next**, then **Install**. No Python or build tools are needed for this path.

Follow the short **[macOS guide](INSTALL_MACOS.md)** or **[Windows guide](INSTALL_WINDOWS.md)**. The [v0.1.0 release](https://github.com/ethanpierce/tdeck-pro-fw/releases/tag/v0.1.0) also has a separate ZIP for each operating system; extract it and open `START-HERE.html`. Internet access is still required for the browser installer.

Installation replaces the current OS and partition table. **The installer does not make a backup.** [Save a full flash backup](#save-the-factory-image-first) first if you need to restore the original firmware. That optional advanced procedure uses the command-line tools below. Leaving Erase device unchecked preserves Handheld OS settings/history in the existing NVS area; data from a different firmware or partition layout may not remain usable. Checking Erase device clears the entire ESP32 flash before installation.

The release contains four separate images at their required offsets. It does not write to NVS at `0x9000–0xDFFF` unless the whole-chip erase option is selected. A single merged image can overwrite that area with padding, even when whole-chip erase is off.

## Advanced: set up the local machine

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

Install `g++` or `clang++` to run `bash tools/test.sh` on Linux/macOS/WSL. The tests run with AddressSanitizer and UndefinedBehaviorSanitizer; they do not need the device. The firmware uses native ESP-IDF 5.4.0, pinned through PlatformIO espressif32 6.10.0. No Arduino framework or Arduino libraries are linked. Production builds use `-O2` optimization with assertions and watchdogs retained. `sdkconfig.defaults` configures the ESP32-S3, USB Serial/JTAG console, QSPI PSRAM, TLS certificates and native NimBLE. Build on any supported PlatformIO host with:

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

## Upload a source build

Return to the repository, with its Python environment active:

```sh
pio run -e tdeck-pro -t upload --upload-port PORT
```

PlatformIO handles the bootloader, partition table, initial OTA metadata and application offsets. This project uses its supplied partition table; the first upload replaces the factory application layout. No separate `erase_flash` step is required. The project does not update modem firmware, SIM settings or fuel-gauge calibration.

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
| `ota_data_initial.bin` | Initial OTA selection data at `0xE000`; generated by native ESP-IDF in the build directory |

Use the PlatformIO upload command for a source build. Flashing the application alone over unknown factory partitions is insufficient.

## Advanced: flash the public release images

Download and extract the macOS or Windows ZIP from the [v0.1.0 release](https://github.com/ethanpierce/tdeck-pro-fw/releases/tag/v0.1.0). Both contain the same firmware, plus platform-specific starter links and instructions. `BUILD_INFO.json` identifies the exact source commit and image hashes; `SHA256SUMS.txt` records the package file checksums. The complete project source is available in the release's `Handheld-OS-v0.1.0-source.zip` and at [source tag v0.1.0](https://github.com/ethanpierce/tdeck-pro-fw/tree/v0.1.0). Pinned dependencies and their sources are listed in [THIRD_PARTY.md](../THIRD_PARTY.md).

After making a backup, close other port users, enter download mode and run this from the extracted package directory. Replace `PORT` with the device's actual port:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 115200 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 firmware/bootloader.bin 0x8000 firmware/partitions.bin 0xE000 firmware/ota_data_initial.bin 0x10000 firmware/firmware.bin
```

Write all four separate images. Do not run `erase_flash` for a normal update and do not install `firmware.bin` alone over unknown factory partitions. The public release uses DIO image headers for compatibility with the browser flasher. Reset normally, re-list the port and capture first boot as described above. Public images have the same hardware validation limits recorded in the release notes.

## Prepare a public release

Commit the intended source changes, remove ignored `config/local.h` overrides, and run:

```sh
python tools/release.py --version v0.1.0
```

The release tool requires a clean checkout, rejects build overrides, performs a clean build, validates the target and partition boundaries, and stages OS ZIPs, a source archive of the exact commit, checksums and the browser installer under `build/release-v0.1.0/`. It does not flash a device or publish files. Read [the installer maintenance notes](../installer/README.md) before publishing the staged site and release assets. Use a new version and output directory for each later release.

`tools/package.py` is retired and exits without building or writing files. Older local handoff ZIPs may contain an ignored `firmware/combined.bin`, `BUILD_INFO.txt`, or a Git bundle produced by `tools/package.py`. These are historical local artifacts, **not the public release packages**. Do not use their merged images for an update or distribute those Git bundles. Use `tools/release.py`, which archives only the selected source commit and emits separate flash images.

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
