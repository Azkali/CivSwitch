#ifndef T3_FFS_DESCRIPTORS_H
#define T3_FFS_DESCRIPTORS_H
#include <stdint.h>

/* Explicit little-endian wire bytes, independent of native struct padding. */
static const uint8_t report_descriptor[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x05,0x07,
    0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02, 0x95,0x01,
    0x75,0x08, 0x81,0x01, 0x95,0x05, 0x75,0x01,
    0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02,
    0x95,0x01, 0x75,0x03, 0x91,0x01, 0x95,0x06,
    0x75,0x08, 0x15,0x00, 0x25,0x65, 0x05,0x07,
    0x19,0x00, 0x29,0x65, 0x81,0x00, 0xc0
};
static const uint8_t hid_descriptor[] = {9,0x21,0x11,0x01,0,1,0x22,63,0};
static const uint8_t ffs_descriptors[] = {
    3,0,0,0,                         /* FunctionFS v2 */
    70,0,0,0,                        /* total bytes */
    3,0,0,0,                         /* HAS_FS_DESC | HAS_HS_DESC */
    3,0,0,0, 3,0,0,0,               /* descriptor counts */
    9,4,0,0,1,3,1,1,1,              /* FS: HID boot keyboard interface */
    9,0x21,0x11,0x01,0,1,0x22,63,0,
    7,5,0x81,3,8,0,1,               /* interrupt IN, 8 bytes, 1 ms */
    9,4,0,0,1,3,1,1,1,              /* HS */
    9,0x21,0x11,0x01,0,1,0x22,63,0,
    7,5,0x81,3,8,0,4                /* 2^(4-1) * 125 us = 1 ms */
};
static const uint8_t ffs_strings[] = {
    2,0,0,0, 33,0,0,0,              /* strings magic, length */
    1,0,0,0, 1,0,0,0, 0x09,0x04,  /* one string, en-US */
    'T','3','S','w','i','t','c','h',' ','H','I','D','K','B',0
};
_Static_assert(sizeof(report_descriptor) == 63, "boot report descriptor size");
_Static_assert(sizeof(ffs_descriptors) == 70, "FFS descriptor block size");
_Static_assert(sizeof(ffs_strings) == 33, "FFS strings block size");
#endif
