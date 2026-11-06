#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static uint8_t transfer[256], out_byte;
static size_t transfer_length;
static int direction, fail_io;
static ssize_t fake_read(int fd, void *buf, size_t n)
{
    (void)fd; direction = 0; transfer_length = n;
    if (fail_io) { errno = EIDRM; return -1; }
    if (n) { memset(buf, 0, n); ((uint8_t *)buf)[0] = out_byte; }
    return (ssize_t)n;
}
static ssize_t fake_write(int fd, const void *buf, size_t n)
{
    (void)fd; direction = 1; transfer_length = n;
    if (fail_io) { errno = EIDRM; return -1; }
    assert(n <= sizeof(transfer));
    if (n) memcpy(transfer, buf, n);
    return (ssize_t)n;
}
#define read fake_read
#define write fake_write
#define main daemon_main
#include "../src/hidkbd_ffs.c"
#undef read
#undef write
#undef main

/* itf0 = gamepad, itf1 = keyboard. */
#define GAMEPAD 0
#define KEYBOARD 1

static void request(struct device *d, uint8_t type, uint8_t req, uint16_t value,
                    uint16_t index, uint16_t length, int expect_direction, size_t expect_length)
{
    struct usb_ctrlrequest s = {.bRequestType = type, .bRequest = req,
        .wValue = htole16(value), .wIndex = htole16(index), .wLength = htole16(length)};
    direction = -1;
    assert(control(d, &s) == 0);
    assert(direction == expect_direction);
    assert(transfer_length == expect_length);
}

int main(void)
{
    struct device d = {.protocol = 1};

    /* Per-interface report and HID descriptors. */
    request(&d, 0x81, 6, 0x2200, KEYBOARD, 255, 1, 63);
    assert(!memcmp(transfer, kbd_report_descriptor, 63));
    request(&d, 0x81, 6, 0x2200, KEYBOARD, 5, 1, 5);
    request(&d, 0x81, 6, 0x2100, KEYBOARD, 9, 1, 9);
    assert(!memcmp(transfer, kbd_hid_descriptor, 9));
    request(&d, 0x81, 6, 0x2200, GAMEPAD, 255, 1, 86);
    assert(!memcmp(transfer, pad_report_descriptor, 86));
    request(&d, 0x81, 6, 0x2100, GAMEPAD, 9, 1, 9);
    assert(!memcmp(transfer, pad_hid_descriptor, 9));

    /* Keyboard class requests (interface 1). */
    request(&d, 0x21, 0x0b, 0, KEYBOARD, 0, 0, 0);
    assert(d.protocol == 0);
    request(&d, 0xa1, 3, 0, KEYBOARD, 1, 1, 1);
    assert(transfer[0] == 0);
    request(&d, 0x21, 0x0b, 1, KEYBOARD, 0, 0, 0);
    assert(d.protocol == 1);
    request(&d, 0x21, 0x0a, 0x7f00, KEYBOARD, 0, 0, 0);
    request(&d, 0xa1, 2, 0, KEYBOARD, 1, 1, 1);
    assert(transfer[0] == 0x7f);
    d.kbd_report[2] = 4;
    request(&d, 0xa1, 1, 0x0100, KEYBOARD, 8, 1, 8);
    assert(transfer[2] == 4);
    out_byte = 0x12;
    request(&d, 0x21, 9, 0x0200, KEYBOARD, 1, 0, 1);
    request(&d, 0xa1, 1, 0x0200, KEYBOARD, 1, 1, 1);
    assert(transfer[0] == 0x12);

    /* Gamepad input report (interface 0) and output report drained over ep0. */
    d.pad_report[0] = 0x04;
    request(&d, 0xa1, 1, 0x0100, GAMEPAD, 8, 1, 8);
    assert(transfer[0] == 0x04);
    request(&d, 0x21, 9, 0x0200, GAMEPAD, 8, 0, 8);

    /* Unsupported requests must use the opposite transfer direction to stall. */
    request(&d, 0x21, 0x0b, 2, KEYBOARD, 0, 1, 0); /* protocol out of range */
    assert(d.protocol == 1);
    request(&d, 0x21, 0x0b, 0, GAMEPAD, 0, 1, 0); /* SET_PROTOCOL not for gamepad */
    request(&d, 0xa1, 3, 0, GAMEPAD, 1, 0, 0);    /* GET_PROTOCOL not for gamepad */
    request(&d, 0xa1, 1, 0x0101, KEYBOARD, 8, 0, 0); /* nonzero report id */
    request(&d, 0x81, 6, 0x2200, 2, 63, 0, 0);    /* interface out of range */
    request(&d, 0x81, 6, 0x2201, KEYBOARD, 63, 0, 0); /* descriptor index set */
    request(&d, 0x21, 9, 0x0200, KEYBOARD, 65535, 1, 0); /* LED report must be 1 byte */
    request(&d, 0x21, 0x0a, 0x1201, KEYBOARD, 0, 1, 0);  /* SET_IDLE report id set */
    assert(d.idle == 0x7f);
    fail_io = 1;
    struct usb_ctrlrequest s = {.bRequestType = 0x21, .bRequest = 0x0b,
        .wIndex = htole16(KEYBOARD)};
    assert(control(&d, &s) == -1 && d.protocol == 1);
    fail_io = 0;

    /* FunctionFS v2 framing: 7 descriptors per speed, composite layout. */
    assert(ffs_descriptors[0] == FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    assert(ffs_descriptors[4] == sizeof(ffs_descriptors));
    assert(ffs_descriptors[8] == (FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC));
    assert(ffs_descriptors[12] == 7); /* FS descriptor count */
    assert(ffs_descriptors[16] == 7); /* HS descriptor count */
    /* Counts are grouped: header 12 + fs_count 4 + hs_count 4 = 20. FS block
     * begins at offset 20; HS block at 20 + 57 = 77. */
    for (unsigned s2 = 0; s2 < 2; ++s2) {
        const uint8_t *b = ffs_descriptors + (s2 ? 77 : 20);
        const uint8_t interval = s2 ? 4 : 1;
        const uint8_t *itf0 = b;
        assert(itf0[0] == 9 && itf0[1] == USB_DT_INTERFACE && itf0[2] == 0);
        assert(itf0[4] == 2 && itf0[5] == 3); /* 2 endpoints, HID class */
        assert(!memcmp(itf0 + 9, pad_hid_descriptor, sizeof(pad_hid_descriptor)));
        const uint8_t *gp_in = itf0 + 18;
        assert(gp_in[0] == 7 && gp_in[1] == USB_DT_ENDPOINT && gp_in[2] == 0x81);
        assert(gp_in[3] == USB_ENDPOINT_XFER_INT && gp_in[4] == 64 && gp_in[6] == interval);
        const uint8_t *gp_out = gp_in + 7;
        assert(gp_out[0] == 7 && gp_out[2] == 0x02 && gp_out[4] == 64);
        const uint8_t *itf1 = gp_out + 7;
        assert(itf1[0] == 9 && itf1[1] == USB_DT_INTERFACE && itf1[2] == 1);
        assert(itf1[4] == 1 && itf1[5] == 3 && itf1[6] == 1 && itf1[7] == 1);
        assert(!memcmp(itf1 + 9, kbd_hid_descriptor, sizeof(kbd_hid_descriptor)));
        const uint8_t *kbd_in = itf1 + 18;
        assert(kbd_in[0] == 7 && kbd_in[1] == USB_DT_ENDPOINT && kbd_in[2] == 0x83);
        assert(kbd_in[3] == USB_ENDPOINT_XFER_INT && kbd_in[4] == 8 && kbd_in[6] == interval);
    }
    puts("Control requests and FunctionFS descriptor framing passed");
    return 0;
}
