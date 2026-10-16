#!/usr/bin/env python3
"""Capture USB diagnostics for a camera-assisted T-Deck Pro bench session."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import sys
import time

import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB serial port, e.g. /dev/ttyACM0 or COM5")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--list", action="store_true", help="List ports and exit")
    args = parser.parse_args()
    if args.list:
        for port in list_ports.comports():
            print(f"{port.device}: {port.description}")
        return 0
    if not args.port or args.seconds <= 0:
        parser.error("--port and a positive --seconds are required")
    output = args.output or Path("logs") / f"bench-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}.log"
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        device = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        device.dtr = False
        device.rts = False
        device.port = args.port
        device.open()
    except serial.SerialException as error:
        print(f"Cannot open {args.port}: {error}", file=sys.stderr)
        return 2
    print(f"Capturing {args.port} to {output}. Point the camera at the screen and keyboard.")
    received = 0
    with device, output.open("w", encoding="utf-8") as log:
        start = time.monotonic()
        pending = [(1.0, "status"), (3.0, "diag"), (35.0, "status")]

        def record(label, text):
            entry = f"{time.monotonic() - start:8.3f} {label} {text}"
            print(entry, end="" if entry.endswith("\n") else "\n", flush=True)
            log.write(entry + ("" if entry.endswith("\n") else "\n"))
            log.flush()

        try:
            while time.monotonic() - start < args.seconds:
                elapsed = time.monotonic() - start
                while pending and elapsed >= pending[0][0]:
                    _, command = pending.pop(0)
                    record("TX", command)
                    device.write((command + "\n").encode("ascii"))
                data = device.read_until(b"\n", 2048)
                if data:
                    received += len(data)
                    record("RX", data.decode("utf-8", errors="replace"))
        except KeyboardInterrupt:
            record("END", "Capture interrupted")
        except serial.SerialException as error:
            record("ERROR", str(error))
            return 2
    if not received:
        print("No serial output received; check the port and firmware.", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
