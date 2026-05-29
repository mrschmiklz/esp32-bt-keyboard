#!/usr/bin/env python3
"""
hardloop.py — one-liner interface to the hardloop daemon.

Usage:
  python hardloop.py "message to type into chat"
  python hardloop.py --status
  python hardloop.py --key ENTER
  python hardloop.py --mod "CTRL+C"

Defaults to COM5 at 115200 baud.
"""

import sys
import os

# Add script dir to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hardloop_daemon import HardloopDaemon
from port_detect import find_esp32_port
import argparse
import serial

def main():
    parser = argparse.ArgumentParser(description="Hardloop: type text into active window via ESP32 BLE keyboard")
    parser.add_argument("text", nargs="?", help="Text to type + Enter")
    parser.add_argument("--status", "-s", action="store_true")
    parser.add_argument("--type", "-t", metavar="TEXT", help="Type without Enter")
    parser.add_argument("--key", "-k", metavar="KEY")
    parser.add_argument("--mod", "-m", metavar="COMBO")
    parser.add_argument("--port", "-p", default=None, help="Serial port (auto-detected if omitted)")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    d = HardloopDaemon(port)
    try:
        d.connect()
    except serial.SerialException as e:
        print(f"ERR: {e}")
        sys.exit(1)

    if args.status:
        print(d.status())
    elif args.text:
        print(d.type_enter(args.text))
    elif args.type:
        print(d.type_text(args.type))
    elif args.key:
        print(d.key(args.key))
    elif args.mod:
        print(d.mod(args.mod))
    else:
        # Read from stdin if no args
        msg = sys.stdin.read().strip()
        if msg:
            print(d.type_enter(msg))
        else:
            parser.print_help()

    d.disconnect()

if __name__ == "__main__":
    main()
