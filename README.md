# ESP32 BLE Keyboard Emulator

Turn an ESP32 into a **Bluetooth Low Energy HID keyboard** that you drive over
USB serial from a host machine (Windows, Linux, Raspberry Pi, macOS). Send text,
special keys, modifier combos, and media keys to any device the ESP32 is paired
with — phone, tablet, laptop, or BIOS/UEFI.

The flagship sketch (`keyboard_v2`) implements the HID stack directly on
[NimBLE](https://github.com/h2zero/NimBLE-Arduino) (no wrapper library), with
bonding, Secure Connections pairing, a consumer-control (media key) report, and
boot-keyboard support for pre-OS environments.

> **Responsible use:** This project advertises itself as a generic HID keyboard
> (with a configurable name/VID/PID) and can only send input to devices you have
> physically paired it with. Use it only on hardware and accounts you own or are
> authorized to test. You are responsible for complying with local laws.

---

## Repository layout

| Path | What it is |
| --- | --- |
| `keyboard_v2/keyboard_v2.ino` | **Main firmware.** Direct NimBLE HID keyboard + media keys, bonding, boot protocol. |
| `keyboard_emulator/keyboard_emulator.ino` | Simpler alternative firmware built on the [T-vK ESP32-BLE-Keyboard](https://github.com/T-vK/ESP32-BLE-Keyboard) library. |
| `ble_hid_receiver/ble_hid_receiver.ino` | Companion sketch for a **second ESP32**: connects to the emulator and decodes/prints received keystrokes (for testing). |
| `ble_scanner/ble_scanner.ino` | Companion sketch: scans for the emulator's advertisement and reports visibility/connection state. |
| `send_keys.py` | Host CLI: open the serial port, send one command, print the response. |
| `hardloop_daemon.py` | Persistent serial daemon — keeps the port open, watches ESP32 events, exposes a REPL and CLI. |
| `hardloop.py` | One-liner wrapper around the daemon (`python hardloop.py "text to type"`). |
| `port_detect.py` | Auto-detects the ESP32 serial port (CP210x / CH340 / FTDI). |
| `setup.sh` | One-shot installer for Linux / Raspberry Pi (deps + serial permissions). |
| `setup.ps1` | One-shot installer for Windows (deps + port detection). |
| `requirements.txt` | Python dependencies (`pyserial`). |

---

## Hardware

- An ESP32 dev board (any variant with BLE).
- A USB cable to the host machine.
- *(Optional)* a second ESP32 to run `ble_hid_receiver` / `ble_scanner` for testing.

---

## Firmware setup (Arduino IDE)

1. Install the **ESP32 board package** (Boards Manager → "esp32" by Espressif).
2. Install **NimBLE-Arduino** (Library Manager → "NimBLE-Arduino") for `keyboard_v2`.
   - For `keyboard_emulator` instead install **ESP32 BLE Keyboard** by T-vK.
3. Open `keyboard_v2/keyboard_v2.ino`, select your board and port, and upload.
4. On your target device, pair with the advertised keyboard (default name
   `Logitech K380` — see *Configuration* below to change it).

### Configuration

Device identity and timing live at the top of `keyboard_v2/keyboard_v2.ino`:

```cpp
#define DEVICE_NAME  "Logitech K380"
#define MANUFACTURER "Logitech"
#define VID          0x046D
#define PID          0xB342
#define KEY_PRESS_MS   20
#define KEY_RELEASE_MS  5
```

---

## Host setup (Python)

Requires Python 3. The host tooling is cross-platform (Windows, Linux, macOS).

**Any platform** — install the dependency directly:

```bash
pip install -r requirements.txt
```

**Linux / Raspberry Pi** — the helper also handles serial-port group permissions:

```bash
chmod +x setup.sh && ./setup.sh
```

**Windows** — run the PowerShell helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
```

**macOS** — use the `pip install` above; the port auto-detects (CP210x/CH340).

The port is auto-detected. Override it with the `HARDLOOP_PORT` environment
variable or the `--port` flag (e.g. `COM5`, `/dev/ttyUSB0`).

### Usage

```bash
# Check connection state
python send_keys.py --status

# Type text (and optionally press Enter)
python send_keys.py --type "Hello world"
python send_keys.py --type-enter "Hello world"

# Special keys and modifier combos
python send_keys.py --key ENTER
python send_keys.py --mod "CTRL+C"
python send_keys.py --mod "WIN+R"

# Interactive REPL
python send_keys.py --interactive

# One-liner via the persistent daemon
python hardloop.py "text to type, then Enter"
```

---

## Serial protocol

The firmware speaks a simple newline-terminated protocol at **115200 baud**:

| Command | Description |
| --- | --- |
| `TYPE <text>` | Type a string. |
| `TYPEN <text>` | Type a string, then press Enter. |
| `KEY <name>` | Special key: `ENTER` `TAB` `ESC` `BACKSPACE` `DELETE` `SPACE` `UP` `DOWN` `LEFT` `RIGHT` `HOME` `END` `PAGEUP` `PAGEDOWN` `F1`–`F12` `CAPS` `INSERT` `PRINTSCREEN`. |
| `MOD <combo>` | Modifier combo, e.g. `CTRL+C`, `WIN+R`, `CTRL+SHIFT+T`. |
| `MEDIA <name>` | Media key (`keyboard_v2` only): `PLAY` `PAUSE` `NEXT` `PREV` `STOP` `MUTE` `VOLUP` `VOLDOWN`. |
| `STATUS` | Returns `READY`, `CONNECTED_NOT_READY`, or `DISCONNECTED`. |
| `BONDS` | List bonded device addresses (`keyboard_v2` only). |
| `PAIR` | Clear all bonds and re-advertise for fresh pairing (`keyboard_v2` only). |

The firmware also emits `EVENT:` lines (e.g. `EVENT:CONNECTED`,
`EVENT:HID_READY`, `EVENT:DISCONNECTED`) that the daemon monitors.

---

## License

See [LICENSE](LICENSE).
