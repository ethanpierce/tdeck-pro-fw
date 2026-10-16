#!/usr/bin/env python3
"""Build verified firmware, browser installer and macOS/Windows release downloads.

This never connects to a device or publishes files. End users need only Chrome or
Edge; Python/PlatformIO are maintainer-side release dependencies.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from urllib.parse import urlencode, urlparse
import zipfile


ROOT = Path(__file__).resolve().parent.parent
ENVIRONMENT = "tdeck-pro"
ESP_WEB_TOOLS_VERSION = "10.4.0"
VERSION_PATTERN = r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?"
SITE_FILES = ("index.html", "style.css", "app.js", "ai-setup.html", "ai-setup.js")
PARTS = (("bootloader.bin", 0x0), ("partitions.bin", 0x8000),
         ("boot_app0.bin", 0xE000), ("firmware.bin", 0x10000))
# Type, subtype, offset, size. The NVS gap must stay unwritten by the installer.
EXPECTED_PARTITIONS = {
    "nvs": (1, 2, 0x9000, 0x5000),
    "otadata": (1, 0, 0xE000, 0x2000),
    "app0": (0, 0x10, 0x10000, 0x600000),
    "app1": (0, 0x11, 0x610000, 0x600000),
    "spiffs": (1, 0x82, 0xC10000, 0x3F0000),
}


def run(*args, cwd=ROOT, env=None):
    return subprocess.check_output(args, cwd=cwd, env=env, text=True).strip()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_sums(directory, filename="SHA256SUMS.txt"):
    paths = sorted(p for p in directory.rglob("*") if p.is_file() and p.name != filename)
    (directory / filename).write_text("".join(
        f"{sha256(p)}  {p.relative_to(directory).as_posix()}\n" for p in paths), encoding="utf-8")


def manifest(version):
    return {
        "name": "Handheld OS",
        "version": version.removeprefix("v"),
        # Without this flag, non-Improv firmware triggers a whole-chip erase.
        # ESP Web Tools 10.4.0 presents an unchecked erase checkbox instead.
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": "ESP32-S3", "parts": [
            {"path": f"firmware/{name}", "offset": offset} for name, offset in PARTS
        ]}],
    }


def validate_images(images):
    """Reject wrong target, unsafe layout, truncated or overlapping images."""
    for name, offset in PARTS:
        if name not in images or not images[name].is_file():
            raise ValueError(f"Missing release image: {name}")
        data = images[name].read_bytes()
        if not data:
            raise ValueError(f"Empty release image: {name}")
        if name in ("bootloader.bin", "firmware.bin"):
            if len(data) < 24 or data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != 9:
                raise ValueError(f"{name} is not an ESP32-S3 image")
            if data[3] >> 4 != 4:
                raise ValueError(f"{name} does not declare 16 MiB flash")
            # esptool CLI normally patches QIO/QOUT headers to DIO; the web
            # installer writes raw bytes, so require the existing DIO header.
            if data[2] != 2:
                raise ValueError(f"{name} is not DIO; do not publish an unpatched web image")
        end = offset + len(data)
        limit = {"bootloader.bin": 0x8000, "partitions.bin": 0x9000,
                 "boot_app0.bin": 0x10000, "firmware.bin": 0x610000}[name]
        if end > limit:
            raise ValueError(f"{name} overlaps another partition or the protected NVS gap")
    if images["boot_app0.bin"].stat().st_size != 0x2000:
        raise ValueError("Unexpected boot_app0.bin size (expected 8192 bytes)")

    table = images["partitions.bin"].read_bytes()
    found = {}
    for start in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from("<HBBII16sI", table, start)
        if magic in (0xFFFF, 0xEBEB):
            break
        if magic != 0x50AA:
            raise ValueError("Invalid binary partition table")
        name = label.split(b"\0", 1)[0].decode("ascii")
        if name in found or flags:
            raise ValueError("Unexpected duplicate or flagged partition")
        found[name] = (kind, subtype, offset, size)
    if found != EXPECTED_PARTITIONS:
        raise ValueError("Partition layout changed; review all release offsets before packaging")


def zip_directory(source, destination, prefix, epoch):
    stamp = datetime.fromtimestamp(epoch, timezone.utc)
    date = (max(1980, stamp.year), stamp.month, stamp.day, stamp.hour, stamp.minute, stamp.second)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(source.rglob("*")):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(f"{prefix}/{path.relative_to(source).as_posix()}", date_time=date)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())


def starter_html(platform, installer_url, version, ai_setup_url):
    url = html.escape(installer_url, quote=True)
    setup_url = html.escape(ai_setup_url, quote=True)
    browser = "Chrome" if platform == "macOS" else "Edge or Chrome"
    return f"""<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Install Handheld OS {html.escape(version)}</title>
<style>body{{font:18px/1.6 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;max-width:650px;margin:60px auto;padding:20px;background:#f5f4ee;color:#202b27}}a{{color:#205e47}}.button{{display:inline-block;background:#205e47;color:white;padding:14px 22px;border-radius:10px;text-decoration:none}}small{{font-size:14px}}</style>
<h1>Install Handheld OS</h1><p>{html.escape(version)} · {html.escape(platform)}</p>
<p>For <strong>T-Deck Pro v1.0/v1.1, A7682E, 915 MHz</strong>. T-Deck Pro MAX is unsupported.</p>
<ol><li>Plug your T-Deck Pro into a USB data port. Keep its battery connected.</li>
<li>Open the installer below in <strong>{browser}</strong>.</li>
<li>Click Connect, choose the Espressif USB device / COM port, then Install.</li>
<li>Leave <strong>Erase device unchecked</strong>. Choose Next, then Install. Wait until it finishes.</li></ol>
<p><a class="button" href="{url}">Open browser installer</a></p>
<p><small>Internet access is required. If this opens in another browser, copy this link into {browser}:<br><a href="{url}">{url}</a></small></p>
<p><small>This replaces installed firmware and the partition table. Back up first if you need a factory restore. Leaving erase unchecked preserves Handheld OS settings and history. See README.txt for setup and recovery.</small></p>
<h2>Optional: set up AI chat</h2>
<p>After installation, <a href="{setup_url}">open AI setup</a> in {browser} to paste your API key directly onto the T-Deck over USB. Setup does not send an AI request.</p>
</html>\n"""


def bundle_readme(platform, version, installer_url, source_url, ai_setup_url):
    browser = "Google Chrome" if platform == "macOS" else "Microsoft Edge or Google Chrome"
    return f"""HANDHELD OS {version} — {platform}

START HERE
1. Plug in your T-Deck Pro with a USB data cable. Keep the battery connected.
2. Open START-HERE.html, then open its installer link in {browser}.
3. Click Connect T-Deck Pro. Select Espressif USB / the device's COM port.
4. Choose Install. Leave Erase device UNCHECKED. Click Next, then Install.
5. Keep USB connected until installation finishes. Tap RESET if Home does not appear.

Installer: {installer_url}
Internet access is required. No terminal, Python, drivers or build tools are
normally needed. Use desktop Chrome or Edge; other browsers are not tested here.

CHECK YOUR HARDWARE
T-Deck Pro v1.0/v1.1 with A7682E modem, 915 MHz radio, ESP32-S3 and 16 MiB flash.
T-Deck Pro MAX and other T-Deck models are unsupported. A chip-family check alone
cannot identify the board model, so check your hardware before installing.

YOUR EXISTING FIRMWARE
This installs firmware and a partition table. Back up first for factory restore:
https://github.com/ethanpierce/tdeck-pro-fw/blob/main/docs/FLASHING.md
Leaving Erase device unchecked preserves Handheld OS saved settings and history.
Data from a different firmware/partition layout might not be retained or usable.

ON THE DEVICE
Home: S opens Settings; M opens Messages; Sym returns Home.
Settings: W sets up Wi-Fi; A edits the cellular APN. Enter saves an APN.
SMS requires an activated SIM, compatible LTE bands/coverage and carrier approval.
An activated AT&T SIM was bench-tested on LTE Band 5 with APN NXTGENPHONE; this is
not a guarantee for another device, area or plan. Keep passwords on the device.

LoRa uses the Handheld OS protocol. Meshtastic is NOT included in this release.
This is an early test release. Calls and Bluetooth pairing remain unverified.

OPTIONAL AI CHAT
After installing, open this link in desktop Chrome or Edge:
{ai_setup_url}
Connect the device, paste your provider's API key and choose Save to T-Deck Pro.
Setup sends settings over USB and makes no AI request. Set up Wi-Fi on the device,
then press A on the AI setup screen and N to write a question. Enter sends it.
The API provider may charge for requests. The device stores the key unencrypted;
keep the device and its flash backups private.

IF THE DEVICE DOES NOT APPEAR
Try a different data cable/direct USB port. Close serial tools or connected tabs.
Hold BOOT, tap RST/RESET, release BOOT, then reconnect in the browser installer.
The USB / COM port can change in bootloader mode. Select the new port.

WHAT IS IN THIS DOWNLOAD
START-HERE.html and the platform shortcut open the exact hosted release above.
firmware/ contains the four raw flash images for advanced recovery, not an app
you run on your computer. Do not flash firmware.bin by itself for a first install.
BUILD_INFO.json records the exact source commit and firmware SHA-256 hashes.
SHA256SUMS.txt checks all files in this download. LICENSE, THIRD_PARTY.md and
docs/licenses/ contain notices; the vendored display license is in lib/GxEPD2/.
FLASH-ADDRESSES.txt lists the four flash addresses for advanced users.

SOURCE AND LICENSE
Project: GPL-3.0-only. Third-party components retain their own licenses.
Exact corresponding source: {source_url}
Full usage and source-build instructions: https://github.com/ethanpierce/tdeck-pro-fw
Problems: https://github.com/ethanpierce/tdeck-pro-fw/issues
Keep phone numbers, IMEIs, SIM identifiers, Wi-Fi credentials and API keys private.
"""


def stage_release(root, output, images, version, repository, site_url, commit, epoch, pio_version):
    """Stage verified inputs. The caller owns clean-source/build verification."""
    validate_images(images)
    stem = f"Handheld-OS-{version}"
    release_url = f"https://github.com/{repository}/releases/tag/{version}"
    asset_base = f"https://github.com/{repository}/releases/download/{version}"
    installer_url = site_url.rstrip("/") + "/?" + urlencode({"version": version})
    ai_setup_url = site_url.rstrip("/") + "/ai-setup.html"
    downloads = {"macos": f"{asset_base}/{stem}-macOS.zip",
                 "windows": f"{asset_base}/{stem}-Windows.zip",
                 "source": f"{asset_base}/{stem}-source.zip",
                 "checksums": f"{asset_base}/{stem}-SHA256SUMS.txt"}
    pages = output / "pages"
    assets = output / "assets"
    release_dir = pages / "releases" / version
    firmware = release_dir / "firmware"
    firmware.mkdir(parents=True)
    assets.mkdir(parents=True)
    for name in SITE_FILES:
        shutil.copy2(root / "installer" / name, pages / name)
    (pages / ".nojekyll").write_text("", encoding="ascii")
    for name, _ in PARTS:
        shutil.copyfile(images[name], firmware / name)

    info = {
        "name": "Handheld OS", "version": version, "source_commit": commit,
        "source_url": f"https://github.com/{repository}/tree/{commit}",
        "source_archive": downloads["source"], "license": "GPL-3.0-only",
        "build_environment": ENVIRONMENT, "platformio": pio_version,
        "platformio_ini_sha256": sha256(root / "platformio.ini"),
        "source_date_epoch": epoch,
        "packaged_utc": datetime.now(timezone.utc).isoformat(),
        "hardware": "T-Deck Pro v1.0/v1.1; A7682E; SX1262 915 MHz; ESP32-S3; 16 MiB flash",
        "unsupported": ["T-Deck Pro MAX", "Other T-Deck models"],
        "esp_web_tools": ESP_WEB_TOOLS_VERSION,
        "whole_flash_erase_default": False,
        "flash_parts": [{"path": name, "offset": offset,
                         "size": images[name].stat().st_size, "sha256": sha256(images[name])}
                        for name, offset in PARTS],
    }
    write_json(release_dir / "BUILD_INFO.json", info)
    write_json(release_dir / "manifest.json", manifest(version))
    release = {"version": version, "source_commit": commit, "release_url": release_url,
               "installer_url": installer_url, "downloads": downloads}
    write_json(release_dir / "release.json", release)
    write_json(pages / "release.json", release)
    write_sums(release_dir)

    # Archive exactly the public release tree. Never clone .git or bundle --all.
    subprocess.run(["git", "archive", "--format=zip", f"--prefix=tdeck-pro-fw-{version}/",
                    "--output", str(assets / f"{stem}-source.zip"), commit], cwd=root, check=True)
    for platform in ("macOS", "Windows"):
        with tempfile.TemporaryDirectory(prefix="handheld-os-download-") as temporary:
            bundle = Path(temporary)
            shutil.copytree(firmware, bundle / "firmware")
            # The bundle manifest lives inside firmware/, so paths are local.
            bundle_manifest = manifest(version)
            for part in bundle_manifest["builds"][0]["parts"]:
                part["path"] = Path(part["path"]).name
            write_json(bundle / "firmware" / "manifest.json", bundle_manifest)
            write_json(bundle / "BUILD_INFO.json", info)
            (bundle / "START-HERE.html").write_text(starter_html(platform, installer_url, version, ai_setup_url), encoding="utf-8")
            (bundle / "README.txt").write_text(bundle_readme(platform, version, installer_url, downloads["source"], ai_setup_url), encoding="utf-8")
            if platform == "macOS":
                (bundle / "Start Installer.webloc").write_text(
                    '<?xml version="1.0" encoding="UTF-8"?>\n'
                    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
                    '<plist version="1.0"><dict><key>URL</key><string>' + html.escape(installer_url) +
                    '</string></dict></plist>\n', encoding="utf-8")
            else:
                (bundle / "Start Installer.url").write_text(
                    f"[InternetShortcut]\nURL={installer_url}\n", encoding="utf-8")
            (bundle / "FLASH-ADDRESSES.txt").write_text(
                "Advanced recovery only: ESP32-S3, 16 MiB flash, DIO.\n"
                "Write these four separate images, without whole-chip erase:\n" +
                "".join(f"0x{offset:05x}  firmware/{name}\n" for name, offset in PARTS) +
                "Do not merge into a padded image for an update: it can overwrite NVS.\n"
                "NVS 0x09000–0x0dfff intentionally has no image.\n", encoding="utf-8")
            for name in ("LICENSE", "THIRD_PARTY.md"):
                shutil.copy2(root / name, bundle / name)
            shutil.copytree(root / "docs" / "licenses", bundle / "docs" / "licenses")
            display_license = bundle / "lib" / "GxEPD2"
            display_license.mkdir(parents=True)
            shutil.copy2(root / "lib" / "GxEPD2" / "LICENSE", display_license / "LICENSE")
            write_sums(bundle)
            zip_directory(bundle, assets / f"{stem}-{platform}.zip", f"{stem}-{platform}", epoch)
    write_sums(assets, f"{stem}-SHA256SUMS.txt")
    return info


def self_test():
    """Exercise real staging, image rejection and archive privacy with fixtures."""
    with tempfile.TemporaryDirectory(prefix="handheld-release-test-") as temporary:
        temp = Path(temporary)
        fixture = temp / "fixture"
        fixture.mkdir()
        shutil.copytree(ROOT / "installer", fixture / "installer")
        (fixture / "platformio.ini").write_text("[env:tdeck-pro]\n", encoding="utf-8")
        (fixture / "LICENSE").write_text("fixture license\n", encoding="utf-8")
        (fixture / "THIRD_PARTY.md").write_text("fixture notices\n", encoding="utf-8")
        (fixture / "docs/licenses").mkdir(parents=True)
        (fixture / "docs/licenses/notice.txt").write_text("fixture notice\n", encoding="utf-8")
        (fixture / "lib/GxEPD2").mkdir(parents=True)
        (fixture / "lib/GxEPD2/LICENSE").write_text("fixture display license\n", encoding="utf-8")
        for args in (("init", "--quiet"), ("add", "."),
                     ("-c", "user.name=Release Test", "-c", "user.email=test@example.invalid",
                      "commit", "--quiet", "-m", "fixture")):
            run("git", *args, cwd=fixture)
        commit = run("git", "rev-parse", "HEAD", cwd=fixture)
        (fixture / "private-not-for-release.txt").write_text("must not ship", encoding="utf-8")
        binaries = temp / "images"
        binaries.mkdir()
        header = bytearray(24)
        header[0], header[2], header[3] = 0xE9, 2, 0x4F
        struct.pack_into("<H", header, 12, 9)
        for name in ("bootloader.bin", "firmware.bin"):
            (binaries / name).write_bytes(header + b"\0" * 256)
        (binaries / "boot_app0.bin").write_bytes(b"\xff" * 0x2000)
        table = bytearray()
        for name, (kind, subtype, offset, size) in EXPECTED_PARTITIONS.items():
            table.extend(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size,
                                     name.encode("ascii"), 0))
        table.extend(b"\xff" * 32)
        (binaries / "partitions.bin").write_bytes(table)
        images = {name: binaries / name for name, _ in PARTS}
        output = temp / "release"
        info = stage_release(fixture, output, images, "v0.1.0", "ethanpierce/tdeck-pro-fw",
                             "https://ethanpierce.github.io/tdeck-pro-fw/", commit, 1760000000, "fixture")
        assert info["source_commit"] == commit
        for name in SITE_FILES:
            assert (output / "pages" / name).read_bytes() == (fixture / "installer" / name).read_bytes()
        parsed = json.loads((output / "pages/releases/v0.1.0/manifest.json").read_text())
        assert parsed["new_install_prompt_erase"] is True
        assert [p["offset"] for p in parsed["builds"][0]["parts"]] == [0, 0x8000, 0xE000, 0x10000]
        for platform in ("macOS", "Windows"):
            with zipfile.ZipFile(output / f"assets/Handheld-OS-v0.1.0-{platform}.zip") as archive:
                names = archive.namelist()
                assert any(n.endswith("/START-HERE.html") for n in names)
                assert sum(n.endswith(".bin") for n in names) == 4
                assert not any(".git/" in n or "private-not" in n for n in names)
                assert any(n.endswith("docs/licenses/notice.txt") for n in names)
                assert any(n.endswith("lib/GxEPD2/LICENSE") for n in names)
                prefix = f"Handheld-OS-v0.1.0-{platform}/"
                setup_url = b"https://ethanpierce.github.io/tdeck-pro-fw/ai-setup.html"
                assert setup_url in archive.read(prefix + "START-HERE.html")
                assert setup_url in archive.read(prefix + "README.txt")
                checksums = archive.read(prefix + "SHA256SUMS.txt").decode()
                for line in checksums.splitlines():
                    digest, path = line.split("  ", 1)
                    assert hashlib.sha256(archive.read(prefix + path)).hexdigest() == digest
        with zipfile.ZipFile(output / "assets/Handheld-OS-v0.1.0-source.zip") as archive:
            assert not any("private-not" in n or ".git/" in n for n in archive.namelist())
        original = images["firmware.bin"].read_bytes()
        for bad in (b"not an ESP image", original[:12] + b"\x00\x00" + original[14:],
                    original[:3] + b"\x2f" + original[4:], b"\0" * 0x600001):
            images["firmware.bin"].write_bytes(bad)
            try:
                validate_images(images)
            except ValueError:
                pass
            else:
                raise AssertionError("Unsafe firmware fixture was accepted")
        images["firmware.bin"].write_bytes(original)
        images["partitions.bin"].write_bytes(b"\0" * 64)
        try:
            validate_images(images)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid partition table fixture was accepted")
    print("Release fixture tests passed: target/layout checks, OS ZIP contents/checksums, exact source archive and erase defaults.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="Release tag, for example v0.1.0")
    parser.add_argument("--output", type=Path, help="New output directory (default: build/release-VERSION)")
    parser.add_argument("--repository", default="ethanpierce/tdeck-pro-fw", help="Public GitHub owner/repository")
    parser.add_argument("--site-url", default="https://ethanpierce.github.io/tdeck-pro-fw/", help="HTTPS GitHub Pages URL")
    parser.add_argument("--pio", type=Path, help="PlatformIO executable (default: local .venv, then PATH)")
    parser.add_argument("--core-dir", type=Path, default=Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio")))
    parser.add_argument("--self-test", action="store_true", help="Run isolated packaging fixtures; do not build firmware")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.version or not re.fullmatch(VERSION_PATTERN, args.version):
        parser.error("--version must be a tag such as v0.1.0 or v0.1.0-rc.1")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        parser.error("--repository must be owner/repository")
    parsed_url = urlparse(args.site_url)
    if parsed_url.scheme != "https" or not parsed_url.netloc or parsed_url.query or parsed_url.fragment:
        parser.error("--site-url must be a plain HTTPS site URL")
    if run("git", "status", "--porcelain", "--untracked-files=normal"):
        parser.error("Commit all intended source changes before release packaging; checkout must be clean")
    if (ROOT / "config/local.h").exists():
        parser.error("config/local.h overrides cannot be included in a public release build")
    overrides = [key for key in os.environ if key.startswith("PLATFORMIO_BUILD_") or
                 key in {"PLATFORMIO_EXTRA_SCRIPTS", "PLATFORMIO_LIB_DEPS", "PLATFORMIO_PLATFORM"}]
    if overrides:
        parser.error("Unset PlatformIO build overrides for a source-verifiable release: " + ", ".join(sorted(overrides)))
    output = (args.output or ROOT / "build" / f"release-{args.version}").resolve()
    if output.exists():
        parser.error(f"Output already exists; choose a new --output directory: {output}")
    candidate = ROOT / ".venv" / ("Scripts/pio.exe" if os.name == "nt" else "bin/pio")
    pio = str(args.pio.resolve()) if args.pio else str(candidate if candidate.exists() else shutil.which("pio") or "")
    if not pio or not Path(pio).is_file():
        parser.error("PlatformIO not found; install requirements-dev.txt or pass --pio /path/to/pio")
    commit = run("git", "rev-parse", "HEAD")
    epoch = int(run("git", "show", "-s", "--format=%ct", "HEAD"))
    env = dict(os.environ, PLATFORMIO_CORE_DIR=str(args.core_dir.resolve()), SOURCE_DATE_EPOCH=str(epoch))
    pio_version = run(pio, "--version", env=env)
    print(f"Release {args.version} from clean commit {commit}", flush=True)
    # Always clean first: an old prebuilt image must never masquerade as HEAD.
    subprocess.run([pio, "run", "-e", ENVIRONMENT, "-t", "clean"], cwd=ROOT, env=env, check=True)
    subprocess.run([pio, "run", "-e", ENVIRONMENT], cwd=ROOT, env=env, check=True)
    if run("git", "status", "--porcelain", "--untracked-files=normal") or run("git", "rev-parse", "HEAD") != commit:
        parser.error("Source changed during build; finish changes, commit and rebuild the release")
    build = ROOT / ".pio" / "build" / ENVIRONMENT
    images = {name: build / name for name, _ in PARTS}
    images["boot_app0.bin"] = args.core_dir.resolve() / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    validate_images(images)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="handheld-os-release-", dir=output.parent) as temporary:
        stage = Path(temporary) / "output"
        info = stage_release(ROOT, stage, images, args.version, args.repository,
                             args.site_url, commit, epoch, pio_version)
        if run("git", "status", "--porcelain", "--untracked-files=normal") or run("git", "rev-parse", "HEAD") != commit:
            parser.error("Source changed during packaging; no release files were published")
        stage.rename(output)
    print(f"\nVerified source: {info['source_commit']}\nGitHub Pages files: {output / 'pages'}\nGitHub Release assets: {output / 'assets'}\nNo device was flashed. No files were published.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Release packaging failed: {error}", file=sys.stderr)
        raise SystemExit(1)
