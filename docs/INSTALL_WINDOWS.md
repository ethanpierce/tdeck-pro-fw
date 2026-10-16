# Install Handheld OS on Windows

**No PowerShell, Python or build tools required.** Use Microsoft Edge or Google Chrome.

For **LILYGO T-Deck Pro v1.0/v1.1 with A7682E and 915 MHz radio** only. **Do not install on T-Deck Pro MAX or the original T-Deck.** This is an experimental preview.

Installing replaces your current OS and may remove saved settings and messages. The installer does not make a backup. If you want to return to your current firmware, [save a full backup first](FLASHING.md#save-the-factory-image-first); that optional advanced step needs command-line tools. Keep backups private.

## Install

1. Leave the battery connected. Plug the T-Deck Pro directly into your PC with a **USB data cable**.
2. In **Edge or Chrome**, open **[the Handheld OS installer](https://ethanpierce.github.io/tdeck-pro-fw/)**.
3. Click **Connect T-Deck Pro**. Select **USB JTAG/serial debug unit**, the Espressif USB serial device, or its **COM** port, then click **Connect**.
4. Choose **Install**. Leave **Erase device unchecked**, click **Next**, then **Install**. Keep the cable connected until installation is complete.
5. Press and release **RST** if the device does not restart. Wait about a minute for the screen and cellular modem to start.

You should see the **Handheld OS** home screen. The e-paper display refreshes slowly.

## Prefer a download?

On the [release page](https://github.com/ethanpierce/tdeck-pro-fw/releases/tag/v0.1.0), download **`Handheld-OS-v0.1.0-Windows.zip`**. Right-click the ZIP, choose **Extract All**, then open **`START-HERE.html`** from the extracted folder. Open its installer link in Edge or Chrome. The download contains the firmware, but installation still uses the online browser installer. Do not choose GitHub's **Source code** download.

## Nothing connects?

Close other serial terminals and browser tabs using the device. Try a different data cable or USB port. Let Windows finish recognizing a newly connected device before retrying.

To enter download mode, use the right-side rocker: hold the **top/BOOT** half, press and release the **lower/RST** half, then release BOOT. Try **Connect T-Deck Pro** again and select the USB device again; its COM number may change. After installation, press RST without holding BOOT.

## Try it

- **Settings:** press **S**. **F** changes the screen light on v1.1; **K** changes the keyboard light.
- **Wi-Fi:** in Settings, press **W**. Turn Wi-Fi on with W if needed, press R to scan, select your network with J/K, then press Enter. Enter the password on the device and press Enter again.
- **Cellular:** an activated, compatible SIM and local coverage are required. AT&T LTE Band 5 and an outbound SMS worked on the bench device with APN **`NXTGENPHONE`**; other SIMs and locations may differ. Set the APN with **Settings → A**. The top bar shows registration, signal bars and battery.
- **Home:** press **Sym** to return home. Press **M** for SMS or **L** for LoRa.

The LoRa app uses this project's **915 MHz custom protocol, not Meshtastic**. Use the correct antenna and a region where this radio configuration is permitted; testing reception needs a second compatible Handheld OS device. Inbound SMS, call audio, BLE keyboard pairing and peer-to-peer LoRa messaging are not fully verified in this preview. Nothing sends an SMS or LoRa message automatically at startup.

See [connectivity setup](CONNECTIVITY.md) for more controls and the [release notes](https://github.com/ethanpierce/tdeck-pro-fw/releases/tag/v0.1.0) for current test results.
