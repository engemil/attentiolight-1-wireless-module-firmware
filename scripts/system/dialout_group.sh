#!/bin/bash

# dialout_group.sh - Add the current user to the 'dialout' group.
# Required for non-root access to serial devices (e.g. /dev/ttyACM0) on Linux.
#
# HOW-TO use:
#   1. chmod +x ./dialout_group.sh
#   2. sudo ./dialout_group.sh
#
# Requirements: must be run with sudo.

# Check if script is run with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)."
    exit 1
fi

# Add current user to the dialout group
usermod -a -G dialout $USER

echo "Please log out and log back in to apply the group changes."
exit 0
