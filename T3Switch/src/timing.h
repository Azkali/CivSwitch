#ifndef T3_TIMING_H
#define T3_TIMING_H
/* PicoSwitch/tools/payload_to_macro.py; verify with tools/check-parity.py. */
enum {
    KEY_HOLD_MS = 40, KEY_GAP_MS = 40,
    ENTER_HOLD_MS = 40, ENTER_GAP_MS = 200,
    PRE_A_WAIT_MS = 800, POST_A_WAIT_MS = 1200,
    PREAMBLE_A_COUNT = 5, PREAMBLE_GAP_MS = 800,
    ESC_TO_A_WAIT_MS = 800, A_PRESS_MS = 50,
    MACRO_START_DELAY_MS = 1000
};
#endif
