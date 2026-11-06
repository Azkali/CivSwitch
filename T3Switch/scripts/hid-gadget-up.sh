#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=common.sh
. "$SCRIPT_DIR/common.sh"
root_lock
configfs_present
udc_free
[ ! -e "$GADGET" ] || die "$GADGET already exists; run hid-gadget-down.sh first"
[ ! -e "$FFS" ] || die "$FFS already exists; inspect it or run hid-gadget-down.sh first"

# HORIPAD (0x0f0d:0x0092): the simple USB Switch controller the Pico emulates.
# It speaks plain HID input reports with no Nintendo handshake, so the console
# accepts its A presses. Callers may override, but the console keys off this pair.
USB_VID=${USB_VID:-0x0f0d}
USB_PID=${USB_PID:-0x0092}
for value in "$USB_VID" "$USB_PID"; do
    case "$value" in
        0x[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) ;;
        *) die 'USB_VID and USB_PID must be four-digit hexadecimal values (0x1234)' ;;
    esac
done
created=0
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$created" = 1 ]; then
        remove_gadget || printf '%s\n' 'rollback incomplete; inspect t3-hidkbd and rerun down' >&2
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
mkdir "$GADGET"
created=1
printf '%s\n' "$USB_VID" > "$GADGET/idVendor"
printf '%s\n' "$USB_PID" > "$GADGET/idProduct"
printf '0x0200\n' > "$GADGET/bcdUSB"
printf '0x0100\n' > "$GADGET/bcdDevice"
printf '0\n' > "$GADGET/bDeviceClass"
printf '0\n' > "$GADGET/bDeviceSubClass"
printf '0\n' > "$GADGET/bDeviceProtocol"
printf '64\n' > "$GADGET/bMaxPacketSize0"
printf 'high-speed\n' > "$GADGET/max_speed"
mkdir "$GADGET/strings/0x409"
# Mirror the Pico HORIPAD strings; the console recognizes the pad by VID/PID
# and descriptors, so these are cosmetic but kept faithful for debugging.
printf 'T3Switch-001\n' > "$GADGET/strings/0x409/serialnumber"
printf 'PICO SWITCH PRO.\n' > "$GADGET/strings/0x409/manufacturer"
printf 'J24 CONTROLLER\n' > "$GADGET/strings/0x409/product"
mkdir "$GADGET/configs/c.1"
# Mirror the HORIPAD configuration: bus-powered, 500 mA, no remote wakeup.
printf '0x80\n' > "$GADGET/configs/c.1/bmAttributes"
printf '500\n' > "$GADGET/configs/c.1/MaxPower"
mkdir "$GADGET/configs/c.1/strings/0x409"
printf 'Gamepad + keyboard\n' > "$GADGET/configs/c.1/strings/0x409/configuration"
mkdir "$GADGET/functions/ffs.hidkbd"
ln -s "$GADGET/functions/ffs.hidkbd" "$GADGET/configs/c.1/ffs.hidkbd"
mkdir "$FFS"
mount -t functionfs -o mode=0600,rmode=0700 hidkbd "$FFS"
created=0
printf '%s\n' 'FunctionFS mounted. UDC remains unbound until hidkbd_ffs writes descriptors.'
