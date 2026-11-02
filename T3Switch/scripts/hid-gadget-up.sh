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

# Prototype IDs; callers with an assigned pair can override both.
USB_VID=${USB_VID:-0x1d6b}
USB_PID=${USB_PID:-0x0104}
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
printf 'T3Switch-001\n' > "$GADGET/strings/0x409/serialnumber"
printf 'T3Switch\n' > "$GADGET/strings/0x409/manufacturer"
printf 'T3Switch FunctionFS keyboard\n' > "$GADGET/strings/0x409/product"
mkdir "$GADGET/configs/c.1"
# T3 uses its own power supply; no remote wakeup is advertised.
printf '0xc0\n' > "$GADGET/configs/c.1/bmAttributes"
printf '0\n' > "$GADGET/configs/c.1/MaxPower"
mkdir "$GADGET/configs/c.1/strings/0x409"
printf 'Boot keyboard\n' > "$GADGET/configs/c.1/strings/0x409/configuration"
mkdir "$GADGET/functions/ffs.hidkbd"
ln -s "$GADGET/functions/ffs.hidkbd" "$GADGET/configs/c.1/ffs.hidkbd"
mkdir "$FFS"
mount -t functionfs -o mode=0600,rmode=0700 hidkbd "$FFS"
created=0
printf '%s\n' 'FunctionFS mounted. UDC remains unbound until hidkbd_ffs writes descriptors.'
