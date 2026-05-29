#!/bin/bash

# udev_rules_esp32.sh - Script to set up udev rules for Espressif ESP32-C3
# (and ESP-Prog / ESP-Prog-2) USB devices on Linux (e.g. Ubuntu).
#
# This script creates a udev rule to allow non-root users to access the ESP32
# USB-Serial-JTAG and ESP-Prog USB devices, and adds the current user to the
# 'plugdev' and 'dialout' groups for USB / serial device access.
#
# Supports:
#   - ESP32-C3 built-in USB-Serial-JTAG (303a:1001)
#   - ESP-Prog-2                        (303a:1002)
#
# HOW-TO use:
#   1. chmod +x ./udev_rules_esp32.sh
#   2. sudo ./udev_rules_esp32.sh
#
# Requirements: must be run with sudo.

# Check if script is run with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)."
    exit 1
fi

# Step 1: Create udev rules file
UDEV_RULES_FILE="/etc/udev/rules.d/50-esp.rules"
echo "Creating udev rules in $UDEV_RULES_FILE..."

echo ""
echo "Content of $UDEV_RULES_FILE:"
echo ""

cat << EOF | tee $UDEV_RULES_FILE
# Espressif ESP32-C3 USB-Serial-JTAG (built into the ESP32-C3 module)
SUBSYSTEMS=="usb", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666", GROUP="plugdev", SYMLINK+="esp_%n"

# Espressif ESP-Prog-2 (external USB-JTAG/Serial bridge)
SUBSYSTEMS=="usb", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1002", MODE="0666", GROUP="plugdev", SYMLINK+="esp_%n"
EOF

echo ""

# Step 2: Set permissions for udev rules file
chmod 644 $UDEV_RULES_FILE

# Step 3: Reload udev rules
echo "Reloading udev rules..."
udevadm control --reload-rules
udevadm trigger

# Step 4: Add user to plugdev and dialout groups
if [ -n "$SUDO_USER" ]; then
    echo "Adding user $SUDO_USER to plugdev and dialout groups..."
    usermod -aG plugdev "$SUDO_USER"
    usermod -aG dialout "$SUDO_USER"
else
    echo "Warning: Could not determine user (SUDO_USER not set). Skipping group addition."
fi

echo ""
echo "Script COMPLETED successfully!"
echo ""

# Step 5: Inform the user
echo "You can verify device detection with 'lsusb'. Expected IDs:"
echo "  ESP32-C3 USB-Serial-JTAG: 303a:1001"
echo "  ESP-Prog-2:               303a:1002"
echo ""

# Optional: Check connected ESP devices
echo "Checking for connected ESP devices with lsusb..."
lsusb | grep "303a:" | grep -E "1001|1002" || echo "No ESP device(s) detected. Ensure the device is connected."

echo ""
echo "Please log out and log back in to apply the group changes."
exit 0
