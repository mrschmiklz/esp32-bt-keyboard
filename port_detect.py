"""
Auto-detect the ESP32 serial port.

Search order:
  1. HARDLOOP_PORT environment variable
  2. CP210x / Silicon Labs USB-UART (most ESP32 devkits)
  3. CH340 / CH341 USB-UART (common on cheaper ESP32 boards)
  4. OS default fallback: /dev/ttyUSB0 (Linux) or COM5 (Windows)
"""

import os
import sys
import serial.tools.list_ports


# Known USB-UART chip signatures found in port descriptions
_ESP32_SIGNATURES = [
    "CP210",       # Silicon Labs CP2102/CP2104
    "Silicon Labs",
    "CH340",       # WCH CH340/CH341
    "CH341",
    "FTDI",        # FT232 (less common on ESP32 but possible)
    "USB Serial",  # Generic fallback
]

_DEFAULT_PORT = {
    "win32":  "COM5",
    "linux":  "/dev/ttyUSB0",
    "linux2": "/dev/ttyUSB0",
    "darwin": "/dev/tty.usbserial-0001",
}


def find_esp32_port() -> str:
    """Return the most likely serial port for the ESP32."""
    env = os.environ.get("HARDLOOP_PORT")
    if env:
        return env

    ports = serial.tools.list_ports.comports()

    # Prefer exact chip matches first
    for sig in _ESP32_SIGNATURES:
        for p in ports:
            desc = (p.description or "") + (p.manufacturer or "")
            if sig.lower() in desc.lower():
                return p.device

    # Last resort: OS default
    return _DEFAULT_PORT.get(sys.platform, "/dev/ttyUSB0")


def list_ports_verbose():
    """Print all detected serial ports with descriptions."""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
        return
    for p in ports:
        marker = " ← likely ESP32" if any(
            s.lower() in ((p.description or "") + (p.manufacturer or "")).lower()
            for s in _ESP32_SIGNATURES
        ) else ""
        print(f"  {p.device:20s}  {p.description}{marker}")
