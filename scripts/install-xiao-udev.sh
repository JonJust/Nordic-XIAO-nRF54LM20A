#!/usr/bin/env bash
# Grant access to this board's debugger and UART without running tools as root.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if (( EUID != 0 )); then
    exec sudo -- bash "$script_dir/install-xiao-udev.sh"
fi

install -o root -g root -m 0644 \
    "$script_dir/60-xiao-nrf54lm20a.rules" \
    /etc/udev/rules.d/60-xiao-nrf54lm20a.rules
udevadm control --reload-rules

# Apply to connected XIAOs and their child interfaces without unplugging them.
for device in /sys/bus/usb/devices/*; do
    if [[ -r "$device/idVendor" && -r "$device/idProduct" ]] &&
       [[ "$(<"$device/idVendor")" == 2886 && "$(<"$device/idProduct")" == 0068 ]]; then
        udevadm trigger --action=change "$device"
        udevadm trigger --action=change --parent-match="$device"
    fi
done
udevadm settle --timeout=10
printf '%s\n' 'XIAO debugger and UART access rules installed and applied.'
