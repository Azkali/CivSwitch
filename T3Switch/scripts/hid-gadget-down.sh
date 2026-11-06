#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=common.sh
. "$SCRIPT_DIR/common.sh"
root_lock
configfs_present
if [ -d "$GADGET" ]; then
    [ "$(cat "$GADGET/strings/0x409/serialnumber")" = 'T3Switch-001' ] ||
        die 'gadget identity differs; refusing to remove it'
fi
remove_gadget || die 'teardown incomplete; check for open endpoints or unexpected configfs entries'
printf '%s\n' 'T3Switch gadget removed and FunctionFS unmounted.'
if [ -r "/sys/class/udc/$UDC/state" ]; then
    printf 'UDC state: %s\n' "$(cat "/sys/class/udc/$UDC/state")"
fi
