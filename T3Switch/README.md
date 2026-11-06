# T3Switch: FunctionFS USB keyboard + gamepad

T3Switch types an opaque, user-supplied file through an XpressReal T3's USB
device port. It presents a composite HORIPAD (0x0f0d:0x0092): a Switch gamepad
interface and a US-QWERTY HID boot keyboard, both with eight-byte reports. It
drives the Civ7 chat bootstrap by interleaving keyboard typing with real
gamepad A presses, the way the Pico does. It does not fetch, generate, or
interpret the file's contents. There is no Pico, kernel rebuild, HID gadget
module, or dependency beyond libc and Linux UAPI headers.

**Validation status:** the fork's Pico source and MIT license were checked.
The host build, fork parity, native T3 build, and ShellCheck pass with the
composite gamepad added. Earlier keyboard-only runs (PC USB enumeration, the
`hello world` and ten-line text smoke tests, root preflight, gadget teardown)
passed before the gadget became a composite controller. USB timestamps and
disconnect handling remain to be verified. Still outstanding on hardware: the
composite HORIPAD gadget enumerating on a PC, and — the real goal — the
gamepad A presses driving the Civ7 chat bootstrap on the Switch 2 end to end.
With the PC cable connected after teardown, the UDC reported `configured` even
though its function, gadget directory and mount were gone. When the PC cable
was unplugged, the port reported host role and the UDC disappeared from
`/sys/class/udc`.

## Prerequisites and hardware

The supplied capability audit (2026-11-02) records T3 kernel
`6.6.54-vendor-realtek-rtd1619b`, aarch64, UDC `98050000.dwc3`, OTG port2,
configfs mounted at `/sys/kernel/config`, and `CONFIG_USB_CONFIGFS_F_FS=y`.
`CONFIG_USB_CONFIGFS_F_HID` is absent, so `/dev/hidg0` cannot be used.

The daemon also requires `CONFIG_AIO=y`; this was confirmed separately on the
T3 and is checked by the preflight. Native kernel AIO uses no
libaio library. It keeps control requests responsive while an interrupt IN
transfer is pending. The helpers require a POSIX shell, coreutils, gzip, grep,
and util-linux (`mount`, `umount`, `findmnt`, `mountpoint`, `flock`). Python 3
and ShellCheck are needed only for development checks.

Power the T3 from its usual supply. Connect **OTG port2** to a **normal PC**
using a data cable before running preflight. On the tested T3 the port switched
to host role and removed the UDC when the cable was unplugged. This validation
does not involve a console. Select a US keyboard layout and turn Caps Lock off
on the PC.

## Build

On T3, from this directory:

```sh
make
./hidkbd_ffs --help
```

Cross-compile on a Linux rig with a Linux aarch64 toolchain (not the bare-metal
devkitA64 compiler):

```sh
make clean
make CROSS_COMPILE=aarch64-linux-gnu-
# Alternatively, with an installed aarch64 musl toolchain:
make clean
make CC=aarch64-linux-musl-gcc LDFLAGS=-static
```

For development on the build host:

```sh
make check
make lint
make parity
```

`make parity` compares the dry-run reports and durations with the adjacent
`PicoSwitch/tools/payload_to_macro.py` on synthetic inputs, including all
printable ASCII, repeated usages, shifted keys, skipped bytes and ten mixed
lines. It reads `PicoSwitch/include/MacroEngine.h` for the start delay. It
does not modify PicoSwitch or consume an application payload. Run it with a
native binary on the host doing the comparison.

## PC bring-up and teardown

Run the root steps manually on T3; its sudo requires your password:

```sh
sudo tools/check-t3.sh
sudo scripts/hid-gadget-up.sh
printf 'hello world\n' > /tmp/hello.txt
sudo ./hidkbd_ffs --payload /tmp/hello.txt --mode text --cps 20 --start-delay-ms 5000
sudo scripts/hid-gadget-down.sh
```

Focus an empty text editor on the PC during the five-second delay. The expected
text is exactly `hello world` followed by one newline. The explicit `--mode text`
is necessary: the Pico macro sends Esc and **two** Enters for each batch, so
its default sequence cannot satisfy a plain-text smoke test.

Preflight exits zero only after checking the UDC is unbound, the configfs
mount, kernel configuration and an actual temporary FunctionFS mount. It
creates a separate unbound probe gadget and removes it on exit. Failure gives
a diagnostic and nonzero exit. It needs root to verify mountability, even if
all the capability checks could otherwise be read without root.

The up script creates only `/sys/kernel/config/usb_gadget/t3-hidkbd`, mounts
instance `hidkbd` at `/dev/ffs-hidkbd`, and leaves `UDC` empty. The daemon
writes descriptors and strings, opens ep1, prepares AIO, then binds the UDC.
It waits up to 30 seconds for enablement before starting the delay. USB transfers
have a five-second timeout. Completion releases all keys, unbinds the UDC,
closes endpoints, and exits. The down script removes the remaining configfs
entries and mount. Stop a running daemon with Ctrl-C before running down.
Helpers and the daemon share a lock; they refuse concurrent operations.

The sample USB IDs `1d6b:0104` are Linux gadget prototype IDs, not an assigned
T3Switch identity. For a device with assigned IDs, set both at setup:

```sh
sudo env USB_VID=0x1234 USB_PID=0x5678 scripts/hid-gadget-up.sh
```

## Typing semantics and options

```text
hidkbd_ffs --payload FILE [--cps N] [--press-ms N]
           [--start-delay-ms N] [--newline enter|cr|none]
           [--mode pico|text] [--dry-run]
```

Default `pico` mode applies the macro's presentation steps to each nonempty
line: five preamble A presses; Esc, A, line, Enter twice; then an A press
between batches. Each A step presses the gamepad A button (`SWITCH_PAD_MASK_A`)
for 50 ms and releases it to neutral, then preserves the following wait. The
keyboard and gamepad are two interfaces of one composite HORIPAD gadget, so A
is a real controller button, not a keyboard key.

| Constant | Milliseconds / count |
| --- | ---: |
| KEY_HOLD_MS / KEY_GAP_MS | 40 / 40 |
| ENTER_HOLD_MS / ENTER_GAP_MS | 40 / 200 |
| PRE_A_WAIT_MS / POST_A_WAIT_MS | 800 / 1200 |
| PREAMBLE_A_COUNT / PREAMBLE_GAP_MS | 5 / 800 |
| ESC_TO_A_WAIT_MS / A_PRESS_MS | 800 / 50 |
| MACRO_START_DELAY_MS | 1000 |

The referenced generator sends different usages back to back. A 40 ms release
is inserted between consecutive equal usages, including case changes on the
same physical key. It does **not** insert a gap after every character. WAIT and
END release the keyboard; PAD presses then releases the gamepad; KEYS replaces
the entire keyboard report. The second
Enter has no trailing gap beyond the next macro step. Timing starts when the
report transfer completes, so USB polling and Linux scheduling add overhead.
Dry-run parity is not proof of physical timing parity.

`text` mode preserves spaces and blank lines, omits the preamble/Esc/pad-A/
double-Enter sequence, and releases after each key. LF, CR and CRLF each map to
one Enter. `--newline enter` and `--newline cr` are aliases: a USB keyboard
sends the same Enter usage for both; the host application decides the resulting
text encoding. `none` drops line terminators in text mode and suppresses the
two batch-ending Enters in Pico mode.

`--cps` accepts integers 1–500 and sets a per-character hold/release period to
`ceil(1000/N)` milliseconds. It inserts a release after every payload character
in either mode. The period must exceed the hold by at least 1 ms; for example,
20 cps with the default hold means 40 ms down and 10 ms released. This is a
pacing override, not parity mode. Special Pico waits and Enter timings remain
unchanged. `--press-ms` accepts 1–60000 and changes payload-key and Esc holds;
`--start-delay-ms` accepts 0–3600000 and starts after USB enablement.

The payload is read completely before binding, up to 16 MiB. Bytes outside the
US map are skipped with a warning containing their hex value and offset, never
the surrounding payload. Pico mode recognizes ASCII `splitlines` separators
and skips whitespace-only or unsupported-only lines. Input is treated as bytes:
UTF-8 is not decoded and Unicode separators/whitespace do not have Python
Unicode semantics. All non-ASCII bytes are unsupported. Tab is supported.

`--dry-run` requires neither root nor USB and prints JSONL steps with the exact
eight-byte report and duration. It performs no delays. Trace files reveal the
typed text through its key usages; store them like the input file.

## Control transfers and failure behavior

The gadget exposes two HID interfaces: interface 0 is the Switch gamepad
(interrupt IN + OUT, eight-byte report), interface 1 is the boot keyboard
(interrupt IN, eight-byte report). Control requests are routed by interface in
`wIndex`. Both serve their report/HID descriptors and GET_REPORT for input; the
keyboard also serves LED GET/SET_REPORT, GET/SET_IDLE, and GET/SET_PROTOCOL
(boot and report protocols share the eight-byte layout). A nonzero keyboard
idle rate resends the current report at the requested interval during waits.
Gamepad output reports over ep0 are accepted and discarded; the declared
gamepad OUT endpoint is not read. Unsupported requests stall ep0.

Disconnect, suspend, endpoint errors, timeout, or SIGINT/SIGTERM/SIGHUP abort
playback and unbind the gadget. There is no automatic replay after reconnection;
restart only after restoring the editor's intended state. Kernel AIO keeps the
interrupt-IN writes from blocking ep0 processing. A final release is sent on
normal completion; on failure, unbinding removes the gadget and clears the
host's device state.

## Limitations and remaining hardware validation

Gamepad A is implemented as a composite HORIPAD (0x0f0d:0x0092) interface and
passes the offline checks (host and native T3 build, fork parity, ShellCheck).
It has **not** yet been validated on hardware: the composite must be shown to
enumerate on a PC, and the A presses must be shown to drive the Civ7 chat
bootstrap on the Switch 2 end to end. Until both pass on-device, this is not a
verified bootstrap replacement. The gadget is always composite now; a PC
typing smoke test still works through its keyboard interface (the PC also sees
an idle gamepad), and `text` mode omits the pad steps entirely.

Only US QWERTY, one key plus modifiers, USB full/high speed, and a self-powered
T3 are described. The gadget's maximum speed is explicitly high speed because
there are no SuperSpeed descriptors. Root is required for both setup and the
daemon's UDC bind; no password handling is implemented.

Record these results on real hardware before closing the adaptation task:

1. Native aarch64 `make`, `make check`, `make parity`, and `make lint` pass.
2. `sudo tools/check-t3.sh` exits zero.
3. PC `lsusb -v` shows class 3/subclass 1/protocol 1 and one interrupt IN
   endpoint; the text-mode smoke test produces exactly one newline.
4. A ten-line synthetic mixed-case/punctuation test matches the macro's order
   and timing. Use PC USB capture timestamps (`usbmon`/Wireshark) for measured
   hold/release intervals; `dmesg` alone establishes enumeration, not timing.
5. Disconnect and Ctrl-C stop typing without automatic replay. Teardown leaves
   no gadget or FunctionFS mount. On the tested T3, after unplugging the PC the
   port switched to host role and the UDC disappeared instead of reporting
   `not attached`.

References: [Pico generator](https://github.com/Gezine/CivSwitch/blob/main/PicoSwitch/tools/payload_to_macro.py),
[MacroEngine](https://github.com/Gezine/CivSwitch/blob/main/PicoSwitch/include/MacroEngine.h),
[Linux FunctionFS](https://docs.kernel.org/usb/functionfs.html),
[Linux 6.6 FunctionFS implementation](https://github.com/torvalds/linux/blob/v6.6/drivers/usb/gadget/function/f_fs.c),
[USB HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf).
