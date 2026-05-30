#!/usr/bin/env bash
#
# list_serial_ports.sh — enumerate USB serial ports for the VS Code task picker.
#
# Emits one line per detected /dev/ttyACM* / /dev/ttyUSB* device in the form:
#
#     <human label>|<port path>
#
# The part before the '|' is shown in the VS Code quick-pick (driven by the
# augustocdias.tasks-shell-input extension, fieldSeparator "|"); the part after
# is substituted into the task as the port to use. The value prefers the stable
# /dev/serial/by-id/ symlink so the choice survives ttyACM renumbering and the
# ESP32-C3 USB-JTAG re-enumeration that happens on every reset.
#
# Vendor/product names come from `lsusb` when available, else from sysfs.
# Espressif (VID 303a) devices are listed first since that's the flash target.

set -euo pipefail

# Build a VID:PID -> "Vendor Product" map from lsusb once (if present).
declare -A LSUSB_NAME
if command -v lsusb >/dev/null 2>&1; then
    while IFS= read -r line; do
        # "Bus 001 Device 004: ID 303a:1001 Espressif USB JTAG/serial debug unit"
        id=$(printf '%s' "$line" | sed -n 's/.* ID \([0-9a-fA-F]\{4\}:[0-9a-fA-F]\{4\}\).*/\1/p')
        name=$(printf '%s' "$line" | sed -n 's/.* ID [0-9a-fA-F]\{4\}:[0-9a-fA-F]\{4\} \(.*\)/\1/p')
        [ -n "$id" ] && LSUSB_NAME["$id"]="$name"
    done < <(lsusb 2>/dev/null)
fi

# Resolve a tty device name (e.g. ttyACM3) to its stable by-id symlink, if any.
byid_for() {
    local dev="$1" link
    for link in /dev/serial/by-id/*; do
        [ -e "$link" ] || continue
        if [ "$(basename "$(readlink -f "$link")")" = "$dev" ]; then
            printf '%s' "$link"
            return 0
        fi
    done
    return 1
}

emit() { # priority|label|port  -> collected, sorted by priority, then printed without priority
    printf '%s\n' "$1"
}

rows=()
for d in /dev/ttyACM* /dev/ttyUSB*; do
    [ -e "$d" ] || continue
    dev=$(basename "$d")

    # Climb sysfs to the USB device node carrying idVendor/idProduct.
    p=$(readlink -f "/sys/class/tty/$dev/device" 2>/dev/null || true)
    while [ -n "$p" ] && [ "$p" != "/" ] && [ ! -f "$p/idVendor" ]; do
        p=$(dirname "$p")
    done

    vid=""; pid=""; prod=""
    if [ -n "$p" ] && [ -f "$p/idVendor" ]; then
        vid=$(cat "$p/idVendor" 2>/dev/null || true)
        pid=$(cat "$p/idProduct" 2>/dev/null || true)
        prod=$(cat "$p/product" 2>/dev/null || true)
    fi

    name="${LSUSB_NAME[${vid}:${pid}]:-$prod}"
    [ -n "$name" ] || name="unknown device"

    # Prefer the stable by-id path as the value handed to the task.
    value="$d"
    if byid=$(byid_for "$dev"); then
        value="$byid"
    fi

    label="${name} [${vid}:${pid}] (${dev})"

    # Espressif first (priority 0), everything else priority 1.
    if [ "$vid" = "303a" ]; then
        rows+=("0|${label}|${value}")
    else
        rows+=("1|${label}|${value}")
    fi
done

if [ ${#rows[@]} -eq 0 ]; then
    echo "no serial ports found|/dev/ttyACM0"
    exit 0
fi

# Sort by leading priority, then strip the priority field before output.
printf '%s\n' "${rows[@]}" | sort | sed 's/^[01]|//'
