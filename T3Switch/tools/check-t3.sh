#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=../scripts/common.sh
. "$SCRIPT_DIR/../scripts/common.sh"
root_lock
configfs_present
udc_free

kernel=$(uname -r)
config=$(mktemp)
probe_gadget=
probe_mount=
mounted=0
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$mounted" = 1 ]; then umount "$probe_mount" || status=1; fi
    if [ -n "$probe_gadget" ]; then
        if [ -d "$probe_gadget/functions/ffs.t3check" ]; then
            rmdir "$probe_gadget/functions/ffs.t3check" || status=1
        fi
        rmdir "$probe_gadget" || status=1
    fi
    if [ -n "$probe_mount" ]; then rmdir "$probe_mount" || status=1; fi
    rm -f "$config"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
if [ -r /proc/config.gz ]; then
    gzip -dc /proc/config.gz > "$config" || die 'cannot decompress /proc/config.gz'
elif [ -r "/boot/config-$kernel" ]; then
    cat "/boot/config-$kernel" > "$config"
elif [ -r "/lib/modules/$kernel/build/.config" ]; then
    cat "/lib/modules/$kernel/build/.config" > "$config"
else
    die "cannot verify kernel capabilities: no readable /proc/config.gz, /boot/config-$kernel, or build/.config"
fi
for cap in USB_GADGET USB_LIBCOMPOSITE CONFIGFS_FS USB_CONFIGFS USB_CONFIGFS_F_FS; do
    grep -Eq "^CONFIG_${cap}=(y|m)$" "$config" || die "CONFIG_$cap is not enabled"
done
grep -q '^CONFIG_AIO=y$' "$config" || die 'CONFIG_AIO=y is required for responsive interrupt IN transfers'

# Test a separate, unbound function; never reconfigure an existing gadget.
probe_mount=$(mktemp -d /run/t3-ffs-check.XXXXXX)
candidate=/sys/kernel/config/usb_gadget/t3-preflight-$$
mkdir "$candidate" || die 'cannot create temporary configfs gadget'
probe_gadget=$candidate
mkdir "$probe_gadget/functions/ffs.t3check" || die 'cannot create FunctionFS function'
mount -t functionfs -o mode=0600,rmode=0700 t3check "$probe_mount" || die 'FunctionFS is not mountable'
mounted=1
[ -e "$probe_mount/ep0" ] || die 'FunctionFS mounted without ep0'
printf '%s\n' "Ready: $UDC is free; configfs, F_FS, AIO and a FunctionFS mount verified."
