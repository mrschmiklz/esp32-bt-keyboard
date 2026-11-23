#!/usr/bin/env python3
"""
detect_controller.py — find the UART port for the *controller* PC.

On dual USB-C ESP32 boards the controlled machine only sees USB HID (no COM port
on the controller). This script lists serial ports and marks likely UART bridges.

Exit 0 always; prints guidance when nothing is found.
"""

from __future__ import annotations

import json
import sys

import serial.tools.list_ports

from port_detect import find_esp32_port, list_ports_verbose

SIGNATURES = ("CP210", "CH340", "CH341", "FTDI", "Silicon Labs", "USB Serial", "UART", "JTAG")


def classify(port) -> str:
    hay = f"{port.description or ''} {port.manufacturer or ''} {port.hwid or ''}"
    if any(s.lower() in hay.lower() for s in SIGNATURES):
        return "likely_controller_uart"
    return "serial"


def main() -> None:
    json_mode = "--json" in sys.argv
    ports = list(serial.tools.list_ports.comports())
    items = []
    for p in ports:
        items.append({
            "device": p.device,
            "description": p.description,
            "manufacturer": p.manufacturer,
            "role": classify(p),
        })

    recommended = find_esp32_port() if ports else None
    payload = {
        "recommended_controller_port": recommended,
        "count": len(ports),
        "ports": items,
        "note": (
            "Controlled PC: native USB (keyboard_usb_s3) or Bluetooth (keyboard_v2). "
            "No COM port on controller for those links."
        ),
        "docs": "docs/DUAL_HOST.md",
    }

    if json_mode:
        print(json.dumps(payload, indent=2))
        return

    if not ports:
        print("No serial (COM) ports detected on this PC.")
        print()
        print("If the ESP32 is plugged in via UART/USB-serial:")
        print("  - Try another cable (data, not charge-only)")
        print("  - Install CP210x / CH340 driver if Windows shows 'Unknown device'")
        print("  - You may be on the native USB port — use the UART-labeled port for control")
        print("  - See docs/DUAL_HOST.md")
        return

    print("Controller UART candidates (use HARDLOOP_PORT or touch-grass serial):")
    list_ports_verbose()
    print()
    print(f"Recommended: {recommended}")
    print("Pin with:  set HARDLOOP_PORT=<device>   (Windows) or export HARDLOOP_PORT=...")


if __name__ == "__main__":
    main()