#!/bin/bash
# Give this user access to the Knight Board FTDI serial port (0403:6001).
# Needs your login password once (sudo).
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
  TARGET_USER="${SUDO_USER:-$USER}"
else
  TARGET_USER="$(id -un)"
  exec sudo -- "$0" "$@"
fi

RULE=/etc/udev/rules.d/99-knight-ftdi.rules
cat > "$RULE" <<'EOF'
# Knight Board — FTDI FT232R USB-UART (0403:6001)
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", GROUP="dialout", MODE="0660", TAG+="uaccess"
EOF

usermod -aG dialout "$TARGET_USER"
udevadm control --reload-rules
udevadm trigger --subsystem-match=tty || true

# Already-plugged adapter is usable immediately (no logout).
if [[ -e /dev/ttyUSB0 ]]; then
  chgrp dialout /dev/ttyUSB0 || true
  chmod 666 /dev/ttyUSB0 || true
fi

echo
echo "USB serial access installed."
echo "  udev rule: $RULE"
echo "  user $TARGET_USER added to group dialout"
echo
echo "This terminal still has the old groups. Either:"
echo "  - run ./np-exg again (it re-execs via sg dialout), or"
echo "  - log out and back in so new sessions inherit dialout permanently."
echo
ls -l /dev/ttyUSB0 /dev/ttyACM0 2>/dev/null || true
id "$TARGET_USER"
