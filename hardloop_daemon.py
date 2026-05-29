#!/usr/bin/env python3
"""
Hardloop Daemon — persistent serial connection to ESP32 BLE keyboard emulator.

Keeps the port open permanently (no DTR reset on each command).
Monitors ESP32 events (CONNECTED, DISCONNECTED, HID_READY) in a background thread.
Accepts commands via stdin or CLI flags.

Usage:
  python hardloop_daemon.py --port COM5                     # interactive REPL
  python hardloop_daemon.py --port COM5 --status            # check status
  python hardloop_daemon.py --port COM5 --type-enter "msg"  # hardloop a message
  python hardloop_daemon.py --port COM5 --cmd "TYPEN hi"    # raw command

Install: pip install pyserial
"""

import argparse
import sys
import os
import time
import threading
import serial
import serial.tools.list_ports

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from port_detect import find_esp32_port, list_ports_verbose


class HardloopDaemon:
    def __init__(self, port, baud=115200, on_event=None):
        self.port = port
        self.baud = baud
        self.ser = None
        self.state = "UNKNOWN"
        # Optional callback invoked (from the background thread) for each
        # "EVENT:" line, after self.state has been updated. Lets a host such
        # as the network bridge push live events. Defaults to None (no-op).
        self.on_event = on_event
        self._lock = threading.Lock()       # serializes all serial I/O
        self._event_thread = None
        self._running = False
        self._response = threading.Event()  # signals a response line arrived
        self._response_line = None          # last non-event line received
        self._in_query = False              # event loop yields when True

    def _open_serial(self):
        ser = serial.Serial()
        ser.port = self.port
        ser.baudrate = self.baud
        ser.timeout = 0.05
        ser.dtr = False   # prevent CP210x from resetting ESP32 on port open
        ser.rts = False
        ser.open()
        ser.reset_input_buffer()
        return ser

    def connect(self):
        self.ser = self._open_serial()
        self._running = True
        self._event_thread = threading.Thread(target=self._event_loop, daemon=True)
        self._event_thread.start()
        self.state = self._query("STATUS")

    def disconnect(self):
        self._running = False
        if self.ser and self.ser.is_open:
            self.ser.close()

    def _event_loop(self):
        buf = b''
        backoff = 1.0
        while self._running:
            if self._in_query:
                time.sleep(0.01)
                continue
            try:
                chunk = self.ser.read(64)
                if chunk:
                    buf += chunk
                    while b'\n' in buf:
                        line, buf = buf.split(b'\n', 1)
                        line = line.decode(errors='replace').strip()
                        if not line:
                            continue
                        if line.startswith("EVENT:"):
                            self._handle_event(line)
                        else:
                            # Unsolicited response (e.g. boot READY) — log it
                            print(f"[daemon] {line}")
            except (serial.SerialException, OSError) as e:
                # USB cable/port dropped — try to reconnect with backoff
                # instead of busy-looping on the dead handle.
                if not self._running:
                    break
                print(f"[daemon] serial error: {e} — reconnecting in {backoff:.0f}s")
                self.state = "DISCONNECTED"
                buf = b''
                try:
                    if self.ser and self.ser.is_open:
                        self.ser.close()
                except Exception:
                    pass
                time.sleep(backoff)
                backoff = min(backoff * 2, 30.0)
                try:
                    self.ser = self._open_serial()
                    print(f"[daemon] reconnected to {self.port}")
                    backoff = 1.0
                except (serial.SerialException, OSError):
                    continue   # still gone — keep retrying
            except Exception:
                # Unexpected, non-fatal: avoid a tight CPU spin.
                time.sleep(0.05)

    def _handle_event(self, event):
        print(f"[daemon] {event}")
        if "HID_READY" in event:
            self.state = "READY"
        elif "HID_UNSUBSCRIBED" in event:
            self.state = "CONNECTED_NOT_READY"
        elif "DISCONNECTED" in event:
            self.state = "DISCONNECTED"
        elif "CONNECTED" in event:
            self.state = "CONNECTED_NOT_READY"
        if self.on_event:
            try:
                self.on_event(event)
            except Exception:
                pass   # never let a consumer callback kill the event loop

    def _query(self, cmd, timeout=20):
        with self._lock:
            self._in_query = True
            try:
                time.sleep(0.02)                # let event loop finish current read
                self.ser.reset_input_buffer()
                self.ser.write((cmd + '\n').encode())
                buf = b''
                deadline = time.time() + timeout
                while time.time() < deadline:
                    chunk = self.ser.read(max(1, self.ser.in_waiting))
                    if chunk:
                        buf += chunk
                        if b'\n' in buf:
                            break
                return buf.decode(errors='replace').strip()
            except (serial.SerialException, OSError) as e:
                return f"ERR:SERIAL {e}"
            finally:
                self._in_query = False

    def status(self):
        resp = self._query("STATUS")
        self.state = resp
        return resp

    def type_text(self, text):
        return self._query(f"TYPE {text}")

    def type_enter(self, text):
        return self._query(f"TYPEN {text}")

    def key(self, name):
        return self._query(f"KEY {name.upper()}")

    def mod(self, combo):
        return self._query(f"MOD {combo.upper()}")

    def media(self, name):
        return self._query(f"MEDIA {name.upper()}")

    def raw(self, cmd):
        return self._query(cmd)

    def wait_ready(self, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.state == "READY":
                return True
            time.sleep(0.2)
        return False

    def repl(self):
        print(f"Hardloop Daemon — {self.port} — state: {self.state}")
        print("Commands: TYPE <text>  TYPEN <text>  KEY <k>  MOD <combo>  MEDIA <k>  STATUS  quit")
        while True:
            try:
                cmd = input('> ').strip()
            except (KeyboardInterrupt, EOFError):
                print()
                break
            if not cmd:
                continue
            if cmd.lower() in ('quit', 'exit', 'q'):
                break
            resp = self.raw(cmd)
            if resp:
                print(resp)


def list_ports():
    list_ports_verbose()


def main():
    parser = argparse.ArgumentParser(
        description='Hardloop Daemon — persistent ESP32 BLE keyboard connection',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('--port', '-p', help='Serial port (auto-detected if omitted; override with HARDLOOP_PORT env var)')
    parser.add_argument('--baud', '-b', default=115200, type=int)
    parser.add_argument('--list-ports', '-l', action='store_true')
    parser.add_argument('--status', '-s', action='store_true')
    parser.add_argument('--type', '-t', metavar='TEXT')
    parser.add_argument('--type-enter', '-T', metavar='TEXT')
    parser.add_argument('--key', '-k', metavar='KEY')
    parser.add_argument('--mod', '-m', metavar='COMBO')
    parser.add_argument('--media', metavar='KEY')
    parser.add_argument('--cmd', metavar='CMD', help='Raw command string')
    parser.add_argument('--wait-ready', action='store_true',
                        help='Wait for HID_READY before sending (up to 30s)')
    parser.add_argument('--interactive', '-i', action='store_true')

    args = parser.parse_args()

    if args.list_ports:
        list_ports()
        return

    port = args.port or find_esp32_port()

    d = HardloopDaemon(port, args.baud)
    try:
        d.connect()
    except serial.SerialException as e:
        print(f"Error: {e}")
        sys.exit(1)

    if args.wait_ready:
        if not d.wait_ready():
            print("ERR:TIMEOUT_WAITING_FOR_READY")
            sys.exit(1)

    if args.status:
        print(d.status())
    elif args.type:
        print(d.type_text(args.type))
    elif args.type_enter:
        print(d.type_enter(args.type_enter))
    elif args.key:
        print(d.key(args.key))
    elif args.mod:
        print(d.mod(args.mod))
    elif args.media:
        print(d.media(args.media))
    elif args.cmd:
        print(d.raw(args.cmd))
    elif args.interactive:
        d.repl()
    else:
        parser.print_help()

    d.disconnect()


if __name__ == '__main__':
    main()
