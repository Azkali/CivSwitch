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
    request(&d, 0x81, 6, 0x2200, 0, 255, 1, 63);
    assert(!memcmp(transfer, report_descriptor, 63));
    request(&d, 0x81, 6, 0x2200, 0, 5, 1, 5);
    request(&d, 0x81, 6, 0x2100, 0, 9, 1, 9);
    assert(!memcmp(transfer, hid_descriptor, 9));
    request(&d, 0x21, 0x0b, 0, 0, 0, 0, 0);
    assert(d.protocol == 0);
    request(&d, 0xa1, 3, 0, 0, 1, 1, 1);
    assert(transfer[0] == 0);
    request(&d, 0x21, 0x0b, 1, 0, 0, 0, 0);
    assert(d.protocol == 1);
    request(&d, 0x21, 0x0a, 0x7f00, 0, 0, 0, 0);
    request(&d, 0xa1, 2, 0, 0, 1, 1, 1);
    assert(transfer[0] == 0x7f);
    d.report[2] = 4;
    request(&d, 0xa1, 1, 0x0100, 0, 8, 1, 8);
    assert(transfer[2] == 4);
    out_byte = 0x12;
    request(&d, 0x21, 9, 0x0200, 0, 1, 0, 1);
    request(&d, 0xa1, 1, 0x0200, 0, 1, 1, 1);
    assert(transfer[0] == 0x12);
    /* Unsupported requests must use the opposite transfer direction to stall. */
    request(&d, 0x21, 0x0b, 2, 0, 0, 1, 0);
    assert(d.protocol == 1);
    request(&d, 0xa1, 1, 0x0101, 0, 8, 0, 0);
    request(&d, 0x81, 6, 0x2200, 1, 63, 0, 0);
    request(&d, 0x81, 6, 0x2201, 0, 63, 0, 0);
    request(&d, 0x21, 9, 0x0200, 0, 65535, 1, 0);
    request(&d, 0x21, 0x0a, 0x1201, 0, 0, 1, 0);
    assert(d.idle == 0x7f);
    fail_io = 1;
    struct usb_ctrlrequest s = {.bRequestType = 0x21, .bRequest = 0x0b};
    assert(control(&d, &s) == -1 && d.protocol == 1);
    /* Verify the packed FS/HS blocks have 3 descriptors and matching HID data. */
    assert(ffs_descriptors[0] == FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    assert(ffs_descriptors[4] == sizeof(ffs_descriptors));
    assert(ffs_descriptors[8] == (FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC));
    for (unsigned i = 0; i < 2; ++i) {
        const uint8_t *p = ffs_descriptors + 20 + i * 25;
        assert(p[0] == 9 && p[1] == USB_DT_INTERFACE);
        assert(p[4] == 1 && p[5] == 3 && p[6] == 1 && p[7] == 1);
        assert(!memcmp(p + 9, hid_descriptor, sizeof(hid_descriptor)));
        p += 18;
        assert(p[0] == 7 && p[1] == USB_DT_ENDPOINT && p[2] == 0x81);
        assert(p[3] == USB_ENDPOINT_XFER_INT && p[4] == 8 && p[5] == 0);
        assert(p[6] == (i ? 4 : 1));
    }
    puts("Control requests and FunctionFS descriptor framing passed");
    return 0;
}
