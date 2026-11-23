# Dual-host deployment (controller + controlled)

Some ESP32 boards expose **two USB-C ports**:

| Port on the board | Typical role | Cable goes to |
| --- | --- | --- |
| **UART / USB‑serial** (CP210x, CH340, or onboard JTAG‑UART) | **Controller** link | The machine running Hermes / `touch-grass` / `hardloop` (serial @ 115200) |
| **Native USB** (ESP32‑S3 USB‑OTG device) | **Controlled** link | The machine that should receive keyboard/mouse input (USB HID) |

## Wiring (after firmware is flashed)

```
  [ Controller PC ]                    [ ESP32 ]                 [ Controlled PC ]
  Hermes / touch-grass                 dual USB‑C
        │                                    │
        │  UART port (serial commands)       │
        └──────────────────────────────────► │
                                             ├──► USB HID ───────►│ types/clicks here
```

**While developing / flashing:** you often use the **UART** port on the same PC
as the IDE — that is normal. In production, only the **UART** cable stays on the
controller; the **native USB** cable goes to the controlled machine.

## Firmware choice (output link)

| Sketch | Output to controlled machine | Serial on controller |
| --- | --- | --- |
| `keyboard_v2/keyboard_v2.ino` | **Bluetooth LE** HID (pair once on controlled PC) | Same `Serial` protocol; use UART port |
| `keyboard_usb_s3/keyboard_usb_s3.ino` | **USB HID** (plug native USB into controlled PC) | UART only (`USB CDC on boot` disabled) |

Both speak the **same newline serial protocol** (`TYPE`, `KEY`, `MOD`, `STATUS`, …)
so `hardloop_daemon.py` and `touch-grass` do not need to change when you switch
link type — only which serial port you open on the controller.

### Bluetooth-only (no second USB to target)

Use **`keyboard_v2`**:

1. UART (or single USB‑serial cable) → controller PC only.
2. Pair the ESP32 as `Logitech K380` on the **controlled** PC over Bluetooth.
3. No native USB cable to the controlled machine required.

This is the default for classic ESP32 (non‑S3) boards and for setups where the
controlled machine is far from the controller or has no free USB port.

### USB HID to controlled (dual cable)

Use **`keyboard_usb_s3`** on an **ESP32‑S3** with a native USB device port:

1. Flash with the **UART** port connected to the controller PC.
2. Set board option **USB CDC On Boot: Disabled** so `Serial` stays on UART (not the programming CDC).
3. Connect **native USB** to the controlled PC (the ESP32 enumerates as a USB keyboard).
4. Controller continues to send commands over UART @ 115200.

## Detecting the controller port on the PC

The **controlled** side will **not** show up as a COM port when using USB HID —
only the **UART** bridge appears on the **controller** machine.

```bash
python detect_controller.py
# or
powershell -ExecutionPolicy Bypass -File setup.ps1 -Detect
```

Pin explicitly when two bridges are present:

```bash
# Linux
export HARDLOOP_PORT=/dev/ttyUSB0
# or stable symlink:
export HARDLOOP_PORT=/dev/serial/by-id/usb-Silicon_Labs_...

# Windows
set HARDLOOP_PORT=COM7
```

## `WHOAMI` (link identity)

Both flagship sketches answer:

```
WHOAMI
```

Examples:

- `LINK:BLE` (`keyboard_v2`)
- `LINK:USB` (`keyboard_usb_s3`)

Use this from automation or future `touch-grass` to confirm output mode without
guessing from the board SKU.

## Status semantics

| `STATUS` | BLE (`keyboard_v2`) | USB (`keyboard_usb_s3`) |
| --- | --- | --- |
| `READY` | Bonded host subscribed to HID | USB host enumerated + HID ready |
| `CONNECTED_NOT_READY` | Connected, no CCCD yet | USB stack up, host not mounted |
| `DISCONNECTED` | Not connected / not advertising | USB not ready |

## Next steps (project roadmap)

When both links work on your dual‑port board:

1. Controller: `hardloop.py --status` → `READY` over UART.
2. Controlled: confirm keystrokes (Notepad test) via BLE **or** USB HID.
3. Wire `touch-grass` with `TOUCH_GRASS_KB_SERIAL` pinned to the UART port.
4. Optional: mirror the same pattern for mouse (`esp32-bt-mouse` BLE + future S3 USB).