#!/usr/bin/env python3
"""Package a committed checkout, Git history and prebuilt ESP32-S3 images."""
import argparse
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parent.parent


def run(*args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT.parent / "tdeck-pro-fw.zip")
    parser.add_argument("--bundle", type=Path, default=ROOT.parent / "tdeck-pro-fw.bundle")
    parser.add_argument("--core-dir", type=Path, default=Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio")))
    args = parser.parse_args()
    if run("git", "status", "--porcelain"):
        parser.error("Commit all source changes before packaging")
    if (ROOT / "config/local.h").exists():
        parser.error("Release packaging uses committed settings; move intended local.h overrides into config/settings.h and commit them first")
    commit = run("git", "rev-parse", "HEAD")
    # Let SCons validate dependency signatures. File timestamps alone reject
    # valid no-op rebuilds, and cannot establish which source an image contains.
    build_env = dict(os.environ, PLATFORMIO_CORE_DIR=str(args.core_dir.resolve()))
    subprocess.run([sys.executable, "-m", "platformio", "run", "-e", "tdeck-pro"],
                   cwd=ROOT, env=build_env, check=True)
    if run("git", "status", "--porcelain") or run("git", "rev-parse", "HEAD") != commit:
        parser.error("Source changed during build; commit it and package again")
    build = ROOT / ".pio/build/tdeck-pro"
    artifacts = [build / f for f in ("firmware.bin", "firmware.elf", "bootloader.bin", "partitions.bin")]
    boot_app = args.core_dir / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    esptool = args.core_dir / "packages/tool-esptoolpy/esptool.py"
    for path in [*artifacts, boot_app, esptool]:
        if not path.is_file():
            parser.error(f"Missing {path}; run pio run -e tdeck-pro first")
    output, bundle = args.output.resolve(), args.bundle.resolve()
    if output.exists() or bundle.exists():
        parser.error("Output already exists; select new --output and --bundle paths")
    output.parent.mkdir(parents=True, exist_ok=True)
    bundle.parent.mkdir(parents=True, exist_ok=True)
    run("git", "bundle", "create", str(bundle), "--all")
    with tempfile.TemporaryDirectory(prefix="handheld-os-package-") as temp:
        checkout = Path(temp) / "tdeck-pro-fw"
        subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", str(ROOT), str(checkout)], check=True)
        run("git", "remote", "remove", "origin", cwd=checkout)
        images = checkout / "firmware"
        images.mkdir()
        for path in [*artifacts, boot_app]:
            shutil.copy2(path, images / path.name)
        shutil.copy2(bundle, checkout / "repository.bundle")
        subprocess.run([
            sys.executable, str(esptool), "--chip", "esp32s3", "merge_bin",
            "--output", str(images / "combined.bin"), "--flash_mode", "keep",
            "--flash_freq", "keep", "--flash_size", "keep",
            "0x0", str(images / "bootloader.bin"), "0x8000", str(images / "partitions.bin"),
            "0xe000", str(images / "boot_app0.bin"), "0x10000", str(images / "firmware.bin"),
        ], check=True)
        (images / "BUILD_INFO.txt").write_text(
            f"Handheld OS 0.1\nCommit: {commit}\nBuilt for: T-Deck Pro A7682E, 915 MHz\n"
            f"Packaged: {datetime.now(timezone.utc).isoformat()}\n"
            "PlatformIO Core 6.1.19; espressif32 6.10.0; Arduino ESP32 2.0.17\n"
            "See docs/BENCH_2026-10-16.md for observed hardware behavior and remaining limits.\n"
            "combined.bin contains bootloader@0, partition table@0x8000, boot_app0@0xe000, app@0x10000.\n"
            "Flash combined.bin at address 0x0 on ESP32-S3; see docs/FLASHING.md.\n",
            encoding="utf-8")
        sums = []
        for path in sorted(images.iterdir()):
            sums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n")
        (images / "SHA256SUMS").write_text("".join(sums), encoding="ascii")
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(checkout.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(checkout.parent))
    print(f"Git commit: {commit}\nArchive: {output}\nGit bundle: {bundle}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
