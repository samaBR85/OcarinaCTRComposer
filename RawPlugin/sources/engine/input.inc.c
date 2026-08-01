// ---- D-pad auto-repeat (typematic) -------------------------------------------------------------
// Menu loops normally use edge detection (pad & ~prev), so a held button fires once. This wraps
// that: hold a D-pad direction and, after a short delay, it keeps firing so long lists scroll
// without mashing. ONLY the D-pad repeats - A/B/X/Y/START/SELECT stay edge-only (else a held A
// would toggle a cheat over and over). Each loop passes its own `prev`; `hold` is shared (only one
// loop runs at a time, and it resets whenever no direction is held).
#define AR_DIRS  (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)
#define AR_DELAY 20   // frames a direction is held before auto-repeat kicks in (~320ms @ 16ms/frame)
#define AR_RATE  4    // then repeat every this many frames (~65ms)
static int g_arHold = 0;
static u32 ARepeat(u32 pad, u32 *prev, int *hold)
{
    u32 down = pad & ~*prev;                 // genuine edges (any button)
    u32 dir  = pad & AR_DIRS;
    if (dir && dir == (*prev & AR_DIRS))     // same direction(s) still held since last frame
    {
        if (++(*hold) >= AR_DELAY && ((*hold - AR_DELAY) % AR_RATE) == 0) down |= dir;
    }
    else *hold = 0;                          // direction changed or released -> restart the delay
    *prev = pad;
    return down;
}
