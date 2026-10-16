#!/usr/bin/env python3
"""Retired local handoff packager. Use the verified public release builder."""
import sys

if __name__ == "__main__":
    print("This legacy packager is retired. Run: python tools/release.py --version vX.Y.Z\n"
          "The release builder uses native ESP-IDF images, preserves the NVS gap, "
          "and archives only the exact source commit.", file=sys.stderr)
    raise SystemExit(2)
