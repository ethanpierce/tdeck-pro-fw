# Installer source

The static browser installer uses the official [ESP Web Tools 10.4.0](https://github.com/esphome/esp-web-tools/releases/tag/10.4.0), pinned in `app.js`. It supports desktop Chrome/Edge on macOS and Windows through Web Serial. Host generated files over HTTPS (or localhost for local inspection); opening `index.html` directly cannot flash devices.

Run the release packager **after committing all intended source changes**:

```sh
.venv/bin/python tools/release.py --self-test
.venv/bin/python tools/release.py --version v0.1.0
```

The second command requires a clean Git checkout, rejects `config/local.h` overrides, cleans and rebuilds the PlatformIO `tdeck-pro` environment, then verifies the source did not change. It does not connect to or flash a device. It validates the ESP32-S3 image headers, 16 MiB flash declaration, exact partition layout, image lengths, offsets and NVS gap before packaging. Packaging reads only the fresh `.pio` outputs and the framework `boot_app0.bin`, never the old ignored `firmware/` folder.

Outputs go to the ignored `build/release-v0.1.0/` folder:

- `pages/`: complete GitHub Pages site, including versioned firmware, manifest and the optional `ai-setup.html` / `ai-setup.js` USB setup page.
- `assets/Handheld-OS-v0.1.0-macOS.zip` and `-Windows.zip`: OS instructions, browser shortcuts, four firmware parts, checksums, build metadata and licenses.
- `assets/Handheld-OS-v0.1.0-source.zip`: exact committed source from `git archive HEAD`, with no Git metadata, private branches or local files.
- `assets/Handheld-OS-v0.1.0-SHA256SUMS.txt`: release asset checksums.

Each platform download starts with `START-HERE.html`, which opens the exact version of the hosted installer. There is no unsigned executable, shell script, Python requirement, or local web server for end users. Firmware files are provided for advanced recovery; those parts are not a standalone offline browser installer.

Both platform guides also link to `ai-setup.html` for optional API-key entry after installation. This page sends settings directly to the connected device with native Web Serial; it uses no external scripts, browser storage or API requests. It needs the same HTTPS host as the installer. Publish both setup files alongside `style.css`; the packager's static asset allowlist and fixtures verify their inclusion.

Publish `pages/` to GitHub Pages and the four files under `assets/` to the GitHub Release for the **same source commit** recorded in `release.json` / `BUILD_INFO.json`. Keep the release as a prerelease while hardware coverage is limited. Preserve previous `pages/releases/<version>/` directories when updating the site so older release shortcuts keep working. Only set a release tag at that recorded commit. `--output`, `--site-url`, `--repository`, `--pio` and `--core-dir` are available for maintainers.

The manifest sets `new_install_prompt_erase: true`: ESP Web Tools otherwise erases all flash for firmware without Improv Serial. In the pinned version, the displayed erase checkbox starts unchecked. The four files intentionally omit NVS at `0x9000`–`0xdfff`; do not merge them into one padded image for the default installer. This preserves Handheld OS settings/history when erase is unchecked, but cannot guarantee preservation of data from a different firmware/partition layout. Installation always changes the bootloader, partition table, OTA metadata and application. A factory restore requires the user's own backup.
