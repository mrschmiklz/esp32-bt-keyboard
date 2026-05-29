#!/usr/bin/env python3
"""
ESP32 BLE Keyboard - Serial Host Client

Usage examples:
  python send_keys.py --port COM3 --status
  python send_keys.py --port COM3 --type "Hello World"
  python send_keys.py --port COM3 --key ENTER
  python send_keys.py --port COM3 --mod "CTRL+C"
  python send_keys.py --port COM3 --mod "WIN+R"
  python send_keys.py --port COM3 --interactive

Install dependency:  pip install pyserial
"""

import argparse
import sys
import os
import time
import serial
import serial.tools.list_ports

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from port_detect import find_esp32_port, list_ports_verbose


def list_ports():
    list_ports_verbose()


def open_serial(port, baud):
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = baud
        ser.timeout = 1
        ser.dtr = False  # prevent CP210x from resetting ESP32 on port open
        ser.rts = False
        ser.open()
    except serial.SerialException as e:
        print(f"Error opening {port}: {e}")
        sys.exit(1)
    ser.reset_input_buffer()
    return ser


def send(ser, cmd, timeout=20):
    ser.reset_input_buffer()  # discard queued events before reading response
    ser.write((cmd + '\n').encode())
    buf = b''
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = ser.read(max(1, ser.in_waiting))
        if chunk:
            buf += chunk
            if b'\n' in buf:
                break
    return buf.decode(errors='replace').strip()


def interactive(ser):
    print("Interactive mode — ESP32 BLE Keyboard")
    print("Commands: TYPE <text>  TYPEN <text>  KEY <name>  MOD <combo>  MEDIA <name>  STATUS  quit")
    print("Special keys: ENTER ESC TAB DELETE BACKSPACE INSERT PRINTSCREEN UP DOWN LEFT RIGHT")
    print("              HOME END PAGEUP PAGEDOWN CAPS F1-F12")
    print("Media keys:   PLAY PAUSE NEXT PREV STOP MUTE VOLUP VOLDOWN  (keyboard_v2 firmware only)")
    while True:
        try:
            cmd = input('> ').strip()
        except (KeyboardInterrupt, EOFError):
            print()
            break
        if not cmd:
            continue
        if cmd.lower() == 'quit':
            break
        resp = send(ser, cmd)
        if resp:
            print(resp)


def main():
    parser = argparse.ArgumentParser(
        description='Serial client for ESP32 BLE keyboard emulator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('--port', '-p', help='Serial port (e.g. COM3 or /dev/ttyUSB0)')
    parser.add_argument('--baud', '-b', default=115200, type=int)
    parser.add_argument('--list-ports', '-l', action='store_true', help='List available serial ports')
    parser.add_argument('--status', '-s', action='store_true', help='Check BLE connection status')
    parser.add_argument('--type', '-t', metavar='TEXT', help='Type a string')
    parser.add_argument('--type-enter', '-T', metavar='TEXT', help='Type a string then press Enter')
    parser.add_argument('--key', '-k', metavar='KEY', help='Press a special key (ENTER, TAB, ESC, F1..F12, etc.)')
    parser.add_argument('--mod', '-m', metavar='COMBO', help='Modifier combo (e.g. CTRL+C, WIN+R, CTRL+SHIFT+T)')
    parser.add_argument('--interactive', '-i', action='store_true', help='Interactive REPL mode')

    args = parser.parse_args()

    if args.list_ports:
        list_ports()
        return

    port = args.port or find_esp32_port()
    ser = open_serial(port, args.baud)

    if args.status:
        print(send(ser, 'STATUS'))
    elif args.type:
        print(send(ser, f'TYPE {args.type}'))
    elif args.type_enter:
        print(send(ser, f'TYPEN {args.type_enter}'))
    elif args.key:
        print(send(ser, f'KEY {args.key.upper()}'))
    elif args.mod:
        print(send(ser, f'MOD {args.mod.upper()}'))
    elif args.interactive:
        interactive(ser)
    else:
        parser.print_help()

    ser.close()


if __name__ == '__main__':
    main()
