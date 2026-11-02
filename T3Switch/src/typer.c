#include <stdio.h>
#include "typer.h"

static int emit(step_sink sink, void *ctx, struct key k, unsigned ms, bool pad)
{
    const uint8_t report[8] = {k.modifier, 0, k.usage, 0, 0, 0, 0, 0};
    return sink(ctx, report, ms, pad);
}
#define STEP(k, ms) do { if (emit(sink, ctx, (k), (ms), false)) return -1; } while (0)
#define WAIT(ms) STEP(released, (ms))
#define PAD() do { if (emit(sink, ctx, released, A_PRESS_MS, true)) return -1; } while (0)

static bool line_end(uint8_t c)
{
    /* Python str.splitlines()'s ASCII separators. Non-ASCII remains opaque. */
    return c == '\n' || c == '\r' || c == '\v' || c == '\f' ||
           c == 0x1c || c == 0x1d || c == 0x1e;
}

int type_payload(const uint8_t *data, size_t size, const struct options *o,
                 step_sink sink, void *ctx)
{
    const struct key released = {0, 0}, enter = {KEY_ENTER, 0}, esc = {KEY_ESCAPE, 0};
    WAIT(o->start_ms);
    if (o->plain) {
        for (size_t i = 0; i < size; ++i) {
            struct key k = ascii_key(data[i]);
            if (data[i] == '\r' || data[i] == '\n') {
                if (data[i] == '\r' && i + 1 < size && data[i + 1] == '\n') ++i;
                if (!o->newline) continue;
                k = enter;
            } else if (!k.usage) {
                fprintf(stderr, "warning: skipping unsupported byte 0x%02x at offset %zu\n", data[i], i);
                continue;
            }
            STEP(k, o->press_ms);
            WAIT(o->gap_ms);
        }
    } else {
        for (unsigned n = 0; n < PREAMBLE_A_COUNT; ++n) {
            PAD(); WAIT(PREAMBLE_GAP_MS);
        }
        bool first = true;
        for (size_t start = 0; start < size;) {
            size_t end = start;
            bool nonspace = false, mapped = false;
            while (end < size && !line_end(data[end])) {
                if (data[end] != ' ' && data[end] != '\t' && data[end] != 0x1f)
                    nonspace = true;
                if (ascii_key(data[end]).usage) mapped = true;
                ++end;
            }
            if (nonspace) {
                for (size_t i = start; i < end; ++i)
                    if (!ascii_key(data[i]).usage)
                        fprintf(stderr, "warning: skipping unsupported byte 0x%02x at offset %zu\n", data[i], i);
            }
            if (nonspace && mapped) {
                if (!first) { WAIT(PRE_A_WAIT_MS); PAD(); WAIT(POST_A_WAIT_MS); }
                first = false;
                STEP(esc, o->press_ms); WAIT(ESC_TO_A_WAIT_MS);
                PAD(); WAIT(POST_A_WAIT_MS);
                uint8_t previous = 0;
                for (size_t i = start; i < end; ++i) {
                    struct key k = ascii_key(data[i]);
                    if (!k.usage) continue;
                    if (!o->paced && k.usage == previous) WAIT(o->gap_ms);
                    STEP(k, o->press_ms);
                    if (o->paced) WAIT(o->gap_ms);
                    previous = k.usage;
                }
                if (o->newline) {
                    STEP(enter, ENTER_HOLD_MS); WAIT(ENTER_GAP_MS);
                    STEP(enter, ENTER_HOLD_MS);
                }
            }
            start = end;
            if (start < size && data[start++] == '\r' && start < size && data[start] == '\n') ++start;
        }
    }
    WAIT(0); /* END always releases all keys. */
    return 0;
}
