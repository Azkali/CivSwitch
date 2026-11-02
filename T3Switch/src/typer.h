#ifndef T3_TYPER_H
#define T3_TYPER_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "keymap.h"
#include "timing.h"

struct options {
    const char *payload;
    unsigned press_ms, gap_ms, start_ms;
    bool plain, newline, paced, dry_run;
};
/* A PAD step holds a released keyboard for A_PRESS_MS and logs its omission. */
typedef int (*step_sink)(void *, const uint8_t report[8], unsigned ms, bool pad);
int type_payload(const uint8_t *, size_t, const struct options *, step_sink, void *);
#endif
