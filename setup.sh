#!/usr/bin/env bash
# setup.sh — install the hardloop host tooling on Linux
#            (Raspberry Pi OS, Pop!_OS, Ubuntu, Debian)
#
# Installs into an isolated virtual environment (.venv) so it works on modern,
# "externally-managed" Python (PEP 668) — i.e. Pi OS Bookworm and Pop!_OS 22.04+,
# where a plain `pip install` is blocked.
#
# Run once:
#   chmod +x setup.sh && ./setup.sh           # core CLI tooling
#   ./setup.sh --bridge                        # also install the network bridge
# Then test:
#   .venv/bin/python hardloop.py --status

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
VENV="$DIR/.venv"
WITH_BRIDGE=0
[ "$1" = "--bridge" ] && WITH_BRIDGE=1

echo "=== Hardloop Setup (Linux) ==="

# 1. Python 3 + venv module
if ! command -v python3 &>/dev/null; then
    echo "Installing python3..."
    sudo apt-get update -qq
    sudo apt-get install -y python3
fi
if ! python3 -c "import venv" &>/dev/null; then
    echo "Installing python3-venv..."
    sudo apt-get update -qq
    sudo apt-get install -y python3-venv
fi

# 2. Virtual environment (avoids PEP 668 externally-managed-environment errors)
if [ ! -d "$VENV" ]; then
    echo "Creating virtual environment in .venv ..."
    python3 -m venv "$VENV"
fi

# 3. Dependencies
echo "Installing Python dependencies..."
"$VENV/bin/pip" install --quiet --upgrade pip
if [ "$WITH_BRIDGE" -eq 1 ]; then
    "$VENV/bin/pip" install --quiet -r "$DIR/requirements-bridge.txt"
else
    "$VENV/bin/pip" install --quiet -r "$DIR/requirements.txt"
fi

# 4. Serial port permissions
# CP210x shows up as /dev/ttyUSB0 on Linux. The user must be in the 'dialout'
# group to access it without sudo.
if ! groups "$USER" | grep -q dialout; then
    echo "Adding $USER to dialout group (required for serial port access)..."
    sudo usermod -a -G dialout "$USER"
    echo ""
    echo "  *** Log out and back in (or reboot) for the group change to take effect. ***"
    echo ""
else
    echo "User $USER already in dialout group — OK"
fi

# 5. Detect ESP32 port
echo ""
echo "Detected serial ports:"
"$VENV/bin/python" -c "
import sys
sys.path.insert(0, '$DIR')
from port_detect import list_ports_verbose, find_esp32_port
list_ports_verbose()
print()
print('Auto-detected port:', find_esp32_port())
"

echo ""
echo "=== Setup complete ==="
echo ""
echo "Usage (run scripts with the venv's Python):"
echo "  .venv/bin/python hardloop.py --status"
echo "  .venv/bin/python hardloop.py \"your message here\""
if [ "$WITH_BRIDGE" -eq 1 ]; then
    echo "  .venv/bin/python bridge.py                  # network bridge (localhost)"
else
    echo ""
    echo "Add the network bridge later with:  ./setup.sh --bridge"
fi
echo ""
echo "Override port:  export HARDLOOP_PORT=/dev/ttyUSB1"
