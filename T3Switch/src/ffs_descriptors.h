#ifndef T3_FFS_DESCRIPTORS_H
#define T3_FFS_DESCRIPTORS_H
#include <stdint.h>
#include "switch_pad.h"

/*
 * Composite gadget mirroring the Pico HORIPAD: interface 0 is the Switch
 * gamepad (interrupt IN + OUT), interface 1 is the boot keyboard (interrupt
 * IN). Endpoint files land in declaration order: ep1 = gamepad IN,
 * ep2 = gamepad OUT, ep3 = keyboard IN. Explicit little-endian wire bytes,
 * independent of native struct padding.
 */
static const uint8_t kbd_report_descriptor[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x05,0x07,
    0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02, 0x95,0x01,
    0x75,0x08, 0x81,0x01, 0x95,0x05, 0x75,0x01,
    0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02,
    0x95,0x01, 0x75,0x03, 0x91,0x01, 0x95,0x06,
    0x75,0x08, 0x15,0x00, 0x25,0x65, 0x05,0x07,
    0x19,0x00, 0x29,0x65, 0x81,0x00, 0xc0
};
static const uint8_t kbd_hid_descriptor[] = {9,0x21,0x11,0x01,0,1,0x22,63,0};

static const uint8_t ffs_descriptors[] = {
    3,0,0,0,                         /* FunctionFS v2 */
    134,0,0,0,                       /* total bytes */
    3,0,0,0,                         /* HAS_FS_DESC | HAS_HS_DESC */

    7,0,0,0,                         /* FS descriptor count */
    9,4,0,0,2,3,0,0,0,              /* itf0: HID gamepad, 2 endpoints */
    9,0x21,0x11,0x01,0,1,0x22,86,0,
    7,5,0x81,3,64,0,1,              /* gamepad interrupt IN, 64 bytes, 1 ms */
    7,5,0x02,3,64,0,1,              /* gamepad interrupt OUT, 64 bytes, 1 ms */
    9,4,1,0,1,3,1,1,0,              /* itf1: HID boot keyboard, 1 endpoint */
    9,0x21,0x11,0x01,0,1,0x22,63,0,
    7,5,0x83,3,8,0,1,              /* keyboard interrupt IN, 8 bytes, 1 ms */

    7,0,0,0,                         /* HS descriptor count */
    9,4,0,0,2,3,0,0,0,
    9,0x21,0x11,0x01,0,1,0x22,86,0,
    7,5,0x81,3,64,0,4,              /* 2^(4-1) * 125 us = 1 ms */
    7,5,0x02,3,64,0,4,
    9,4,1,0,1,3,1,1,0,
    9,0x21,0x11,0x01,0,1,0x22,63,0,
    7,5,0x83,3,8,0,4
};
static const uint8_t ffs_strings[] = {
    2,0,0,0, 31,0,0,0,              /* strings magic, length */
    1,0,0,0, 1,0,0,0, 0x09,0x04,  /* one string, en-US */
    'T','3','S','w','i','t','c','h',' ','p','a','d',0
};
_Static_assert(sizeof(kbd_report_descriptor) == 63, "boot report descriptor size");
_Static_assert(sizeof(ffs_descriptors) == 134, "FFS descriptor block size");
_Static_assert(sizeof(ffs_strings) == 31, "FFS strings block size");
#endif
