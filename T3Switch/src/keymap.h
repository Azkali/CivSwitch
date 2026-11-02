#ifndef T3_KEYMAP_H
#define T3_KEYMAP_H
#include <stdint.h>

enum { KEY_ENTER = 0x28, KEY_ESCAPE = 0x29, MOD_SHIFT = 0x02 };
struct key { uint8_t usage, modifier; };

/* US QWERTY; unsupported bytes return usage zero. No locale or text decoding. */
static inline struct key ascii_key(unsigned char c)
{
    struct key k = {0, 0};
    if (c >= 'a' && c <= 'z') k.usage = (uint8_t)(4 + c - 'a');
    else if (c >= 'A' && c <= 'Z') {
        k.usage = (uint8_t)(4 + c - 'A'); k.modifier = MOD_SHIFT;
    } else if (c >= '1' && c <= '9') k.usage = (uint8_t)(0x1e + c - '1');
    else {
        switch (c) {
        case '0': k.usage = 0x27; break;
        case '\t': k.usage = 0x2b; break;
        case ' ': k.usage = 0x2c; break;
        case '!': k = (struct key){0x1e, MOD_SHIFT}; break;
        case '@': k = (struct key){0x1f, MOD_SHIFT}; break;
        case '#': k = (struct key){0x20, MOD_SHIFT}; break;
        case '$': k = (struct key){0x21, MOD_SHIFT}; break;
        case '%': k = (struct key){0x22, MOD_SHIFT}; break;
        case '^': k = (struct key){0x23, MOD_SHIFT}; break;
        case '&': k = (struct key){0x24, MOD_SHIFT}; break;
        case '*': k = (struct key){0x25, MOD_SHIFT}; break;
        case '(': k = (struct key){0x26, MOD_SHIFT}; break;
        case ')': k = (struct key){0x27, MOD_SHIFT}; break;
        case '-': k.usage = 0x2d; break;
        case '_': k = (struct key){0x2d, MOD_SHIFT}; break;
        case '=': k.usage = 0x2e; break;
        case '+': k = (struct key){0x2e, MOD_SHIFT}; break;
        case '[': k.usage = 0x2f; break;
        case '{': k = (struct key){0x2f, MOD_SHIFT}; break;
        case ']': k.usage = 0x30; break;
        case '}': k = (struct key){0x30, MOD_SHIFT}; break;
        case '\\': k.usage = 0x31; break;
        case '|': k = (struct key){0x31, MOD_SHIFT}; break;
        case ';': k.usage = 0x33; break;
        case ':': k = (struct key){0x33, MOD_SHIFT}; break;
        case '\'': k.usage = 0x34; break;
        case '"': k = (struct key){0x34, MOD_SHIFT}; break;
        case '`': k.usage = 0x35; break;
        case '~': k = (struct key){0x35, MOD_SHIFT}; break;
        case ',': k.usage = 0x36; break;
        case '<': k = (struct key){0x36, MOD_SHIFT}; break;
        case '.': k.usage = 0x37; break;
        case '>': k = (struct key){0x37, MOD_SHIFT}; break;
        case '/': k.usage = 0x38; break;
        case '?': k = (struct key){0x38, MOD_SHIFT}; break;
        default: break;
        }
    }
    return k;
}
#endif
