#!/bin/sh
# Sourced by the root helpers. Fixed paths match src/hidkbd_ffs.c.
GADGET=/sys/kernel/config/usb_gadget/t3-hidkbd
FFS=/dev/ffs-hidkbd
UDC=98050000.dwc3

die() { printf '%s\n' "error: $*" >&2; exit 1; }
root_lock() {
    [ "$(id -u)" = 0 ] || die 'run this helper with sudo'
    umask 077
    mkdir -p /run/lock
    # Never unlink the lock: all helpers and the daemon must lock the same inode.
    exec 9>/run/lock/t3-hidkbd.lock
    flock -n 9 || die 'another typer/helper is running; stop it before teardown'
}
configfs_present() {
    [ "$(findmnt -rn -M /sys/kernel/config -o FSTYPE)" = configfs ] ||
        die 'configfs is not mounted at /sys/kernel/config'
    [ -d /sys/kernel/config/usb_gadget ] || die 'usb_gadget configfs subsystem is unavailable'
}
udc_free() {
    [ -d "/sys/class/udc/$UDC" ] || die "UDC $UDC does not exist"
    for node in /sys/kernel/config/usb_gadget/*/UDC; do
        [ -f "$node" ] || continue
        [ "$(cat "$node")" != "$UDC" ] || die "$UDC is bound by $node"
    done
    if [ -r "/sys/class/udc/$UDC/function" ]; then
        [ -z "$(cat "/sys/class/udc/$UDC/function")" ] || die "$UDC already has a gadget driver"
    fi
}
remove_gadget() {
    if [ -f "$GADGET/UDC" ] && [ -n "$(cat "$GADGET/UDC")" ]; then
        printf '\n' > "$GADGET/UDC" || return 1
    fi
    if mountpoint -q "$FFS"; then
        [ "$(findmnt -rn -M "$FFS" -o FSTYPE)" = functionfs ] || return 1
        [ "$(findmnt -rn -M "$FFS" -o SOURCE)" = hidkbd ] || return 1
        umount "$FFS" || return 1
    fi
    if [ -L "$GADGET/configs/c.1/ffs.hidkbd" ]; then
        rm "$GADGET/configs/c.1/ffs.hidkbd" || return 1
    fi
    for node in "$GADGET/configs/c.1/strings/0x409" "$GADGET/configs/c.1" \
                "$GADGET/functions/ffs.hidkbd" "$GADGET/strings/0x409" "$GADGET"; do
        if [ -d "$node" ]; then rmdir "$node" || return 1; fi
    done
    if [ -d "$FFS" ]; then rmdir "$FFS" || return 1; fi
}
