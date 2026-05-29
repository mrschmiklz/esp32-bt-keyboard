#!/usr/bin/env bash
# setup.sh — install hardloop system on Linux (Raspberry Pi, Ubuntu, Debian)
#
# Run once:  chmod +x setup.sh && ./setup.sh
# Then test: python3 hardloop.py --status

set -e

echo "=== Hardloop Setup ==="

# 1. Python 3 + pip
if ! command -v python3 &>/dev/null; then
    echo "Installing python3..."
    sudo apt-get update -qq
    sudo apt-get install -y python3 python3-pip
fi

# 2. pyserial
echo "Installing Python dependencies..."
pip3 install --quiet -r "$(dirname "$0")/requirements.txt"

# 3. Serial port permissions
# CP210x shows up as /dev/ttyUSB0 on Linux.
# User must be in the 'dialout' group to access it without sudo.
if ! groups "$USER" | grep -q dialout; then
    echo "Adding $USER to dialout group (required for serial port access)..."
    sudo usermod -a -G dialout "$USER"
    echo ""
    echo "  *** You must log out and back in (or reboot) for group change to take effect. ***"
    echo ""
else
    echo "User $USER already in dialout group — OK"
fi

# 4. Detect ESP32 port
echo ""
echo "Detected serial ports:"
python3 -c "
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath('$0')))
from port_detect import list_ports_verbose, find_esp32_port
list_ports_verbose()
print()
print('Auto-detected port:', find_esp32_port())
"

echo ""
echo "=== Setup complete ==="
echo ""
echo "Usage:"
echo "  python3 hardloop.py --status"
echo "  python3 hardloop.py \"your message here\""
echo ""
echo "Override port:  export HARDLOOP_PORT=/dev/ttyUSB1"
echo "Explicit port:  python3 hardloop.py --port /dev/ttyUSB0 --status"
