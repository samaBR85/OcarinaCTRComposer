// ===================== Tools =====================
// These run from inside RunMenu, so the game is already paused (memory stable).

// ---- Touch keypad (bottom screen). hid:USER is in the game's ACL, so unlike
// ir:rst we CAN read the touch panel. HID runs in its own process, so touch
// keeps updating even while the game threads are frozen. ----
// IMPORTANT: do NOT use libctru's hidInit() — on New 3DS it calls irrstInit()
// internally (hidShouldUseIrrst()), and ir:rst conflicts with OoT3D and FREEZES
// the game (+ blocks the HOME button). We do a minimal manual hid:USER init:
// open the service, map the shared memory, read touch directly. No ir:rst.
static int   hidReady;
static vu32 *hidShmem;

static void KbInit(void)
{
    if (hidReady) return;
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

    Handle srv = 0;
    if (R_FAILED(srvGetServiceHandle(&srv, "hid:USER")) &&
        R_FAILED(srvGetServiceHandle(&srv, "hid:SPVR"))) return;

    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(0xA, 0, 0); // HIDUSER_GetIPCHandles
    Result r = svcSendSyncRequest(srv);
    if (R_SUCCEEDED(r)) r = (Result)cmd[1];
    Handle mem = 0, ev[5] = { 0 };
    if (R_SUCCEEDED(r)) { mem = cmd[3]; for (int i = 0; i < 5; ++i) ev[i] = cmd[4 + i]; }
    svcCloseHandle(srv);
    for (int i = 0; i < 5; ++i) if (ev[i]) svcCloseHandle(ev[i]); // events unused
    if (R_FAILED(r) || !mem) return;

    vu32 *sh = (vu32 *)mappableAlloc(0x2B0);
    if (!sh) { svcCloseHandle(mem); return; }
    if (R_FAILED(svcMapMemoryBlock(mem, (u32)sh, MEMPERM_READ, MEMPERM_DONTCARE)))
    { svcCloseHandle(mem); return; }

    hidShmem = sh;
    hidReady = 1;
}

// Read the touch panel directly from HID shared memory (layout per libctru:
// touch section at word 42, index at 42+4, entries {pos,valid} at 42+8+Id*2).
static int HidTouch(int *px, int *py)
{
    if (!hidShmem) return 0;
    u32 id = hidShmem[42 + 4]; if (id > 7) id = 7;
    u32 packed = hidShmem[42 + 8 + id * 2];
    u32 valid  = hidShmem[42 + 8 + id * 2 + 1];
    if (px) *px = (int)(packed & 0xFFFF);
    if (py) *py = (int)((packed >> 16) & 0xFFFF);
    return valid != 0;
}

// hit-test a touch against a key rect
static int KbHit(int tx, int ty, int x, int y, int w, int h)
{ return tx >= x && tx < x + w && ty >= y && ty < y + h; }

// one key, styled by kind. kinds: 0 num, 1 del/clr, 2 hex-on, 3 hex-off,
// 4 toggle(gold), 5 OK(green), 6 Cancel(red)
static void KbKey(int x, int y, int w, int h, const char *label, int kind, int hot)
{
    u8 br, bg, bb, tr, tg, tb;
    switch (kind)
    {
        case 1:  br=40; bg=33; bb=20; tr=246; tg=236; tb=210; break; // Del/Clr
        case 3:  br=34; bg=28; bb=17; tr=140; tg=130; tb=104; break; // hex off
        case 4:  br=46; bg=62; bb=42; tr=236; tg=200; tb=120; break; // toggle
        case 5:  br=36; bg=70; bb=34; tr=180; tg=240; tb=170; break; // OK
        case 6:  br=74; bg=42; bb=30; tr=240; tg=180; tb=160; break; // Cancel
        default: br=54; bg=44; bb=26; tr=246; tg=236; tb=210; break; // num / hex on
    }
    if (hot) { br += 30; bg += 30; bb += 22; }
    CFill(x, y, w, h, br, bg, bb);
    CFill(x, y, w, 1, GOLD); CFill(x, y + h - 1, w, 1, GOLD);
    CFill(x, y, 1, h, GOLD); CFill(x + w - 1, y, 1, h, GOLD);
    int tw = CTextWidth(label);
    CText(x + (w - tw) / 2, y + (h - 15) / 2, label, tr, tg, tb, 1);
}

// Layout B: phone numpad + hex block + bottom OK/Cancel bar.
// codes: 0-15 = digit value; 20 Del; 21 Clr; 22 toggle; 23 OK; 24 Cancel
typedef struct { int x, y, w, h, code; const char *lab; } KbBtn;

static int KbBuild(KbBtn *b, int hex)
{
    // Coordinates measured from the user's 320x240 reference layout (kbd/position guide). Every key is
    // a sprite (see kbkeys.h). kw/kh = 52x30, pitch 57 x / 35 y.
    int n = 0, kw = 52, kh = 30, XP = 57, YP = 35, x0 = 16, y0 = 53;
    static const int   pc[12] = { 1,2,3, 4,5,6, 7,8,9, 20,0,21 };
    static const char *pl[12] = { "1","2","3","4","5","6","7","8","9","Del","0","Clr" };
    for (int i = 0; i < 12; ++i)
    { int c = i % 3, r = i / 3;
      b[n++] = (KbBtn){ x0 + c*XP, y0 + r*YP, kw, kh, pc[i], pl[i] }; }

    int hx = 196;
    static const int   hc[6] = { 10,11,12,13,14,15 };
    static const char *hl[6] = { "A","B","C","D","E","F" };
    for (int i = 0; i < 6; ++i)
    { int c = i % 2, r = i / 2;
      b[n++] = (KbBtn){ hx + c*XP, y0 + r*YP, kw, kh, hc[i], hl[i] }; }

    b[n++] = (KbBtn){ hx, 158, 109, 30, 22, hex ? "HEX" : "DEC" };
    b[n++] = (KbBtn){ 51,  198, 96, 32, 23, "OK" };
    b[n++] = (KbBtn){ 173, 198, 96, 32, 24, "Cancel" };
    return n;
}

// Touch keypad entry (decimal, with HEX toggle). Returns value; *cancel on Cancel.
static u32 EnterNum(const char *title, u32 initial, int *cancel)
{
    KbInit();
    *cancel = 0;
    u32 val = initial;
    int hex = 0, changed = 1, hotCode = -1;
    int touchPrev = HidTouch(0, 0); // ignore a touch already held on entry

    while (1)
    {
        KbBtn b[24];
        int nb = KbBuild(b, hex);

        if (changed)
        {
            // dim frozen bottom frame as backdrop
            for (int y = 0; y < BOT_H; ++y)
                for (int x = 0; x < BOT_W; ++x)
                {
                    u8 *p = CPix(x, y);
                    if (savedBotValid)
                    { u16 v = savedBot[y * BOT_W + x];
                      p[0] = (u8)((((v>>11)&31)<<3)/3); p[1] = (u8)((((v>>5)&63)<<2)/3); p[2] = (u8)(((v&31)<<3)/3); }
                    else { p[0] = p[1] = p[2] = 12; }
                }
            CText(14, 8, title, GOLD, 1);
            char disp[40];
            if (hex) siprintf(disp, "0x%lX", (unsigned long)val);
            else     siprintf(disp, "%lu  (0x%lX)", (unsigned long)val, (unsigned long)val);
            CFill(12, 30, 296, 18, 18, 13, 7);
            CText(18, 31, disp, INK, 0);

            for (int i = 0; i < nb; ++i)
            {
                int c = b[i].code, x = b[i].x, y = b[i].y, w = b[i].w, h = b[i].h;
                int hot = (hotCode == c);
                if (c <= 9)        // digit sprite (kb_digits[value]: 00.png=0 .. 09.png=9)
                    DrawKbSprite(x, y, w, h, kb_digits[c], KB_KEY_W, KB_KEY_H, 0);
                else if (c <= 15)  // hex A-F sprite, dimmed while in DEC mode (disabled)
                    DrawKbSprite(x, y, w, h, kb_hex[c - 10], KB_KEY_W, KB_KEY_H, hex ? 0 : 150);
                else if (c == 20)  // Del (backspace) sprite
                    DrawKbSprite(x, y, w, h, kb_del, KB_KEY_W, KB_KEY_H, 0);
                else if (c == 21)  // Clr sprite
                    DrawKbSprite(x, y, w, h, kb_clr, KB_KEY_W, KB_KEY_H, 0);
                else if (c == 22)  // DEC/HEX toggle sprite (label reflects the CURRENT mode)
                    DrawKbSprite(x, y, w, h, hex ? kb_hextog : kb_dec, KB_TOG_W, KB_TOG_H, 0);
                else if (c == 23)  // OK sprite
                    DrawKbSprite(x, y, w, h, kb_ok, KB_OK_W, KB_OK_H, 0);
                else               // Cancel sprite
                    DrawKbSprite(x, y, w, h, kb_cancel, KB_CANCEL_W, KB_CANCEL_H, 0);
                if (hot && c != 22) CFillBlend(x, y, w, h, 255, 255, 255, 45); // press flash (not the toggle: swapping DEC/HEX is feedback enough, and the flash would persist)
            }
            BotBlitComposeBoth();
            changed = 0;
        }

        svcSleepThread(16 * 1000 * 1000);
        if (HID_PAD & BUTTON_B) { *cancel = 1; return initial; } // physical B cancels

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;

        for (int i = 0; i < nb; ++i)
        {
            if (!KbHit(px, py, b[i].x, b[i].y, b[i].w, b[i].h)) continue;
            int c = b[i].code;
            if (c <= 15)
            {
                if (c >= 10 && !hex) break;             // hex digit disabled in dec
                u32 base = hex ? 16 : 10, nv = val * base + c;
                if (nv >= val) val = nv;                 // ignore overflow
                hotCode = c;
            }
            else if (c == 20) { val = hex ? (val >> 4) : (val / 10); hotCode = c; }
            else if (c == 21) { val = 0; hotCode = c; }
            else if (c == 22) { hex = !hex; hotCode = c; }
            else if (c == 23) return val;
            else { *cancel = 1; return initial; }
            changed = 1;
            break;
        }
    }
}

// ---- memory region walk (game's own RW private regions) ----
// Region clamp for the walk (set from the Cheat Search "Memory Region" preset).
static u32 g_scanLo, g_scanHi;
typedef void (*RegionCb)(u32 base, u32 size, void *ud);
static void ForEachRWRegion(RegionCb cb, void *ud)
{
    u32 lo = g_scanLo ? g_scanLo : 0x00100000;
    u32 hi = g_scanHi ? g_scanHi : 0x40000000;
    u32 addr = lo;
    while (addr < hi)
    {
        MemInfo info; PageInfo pg;
        if (R_FAILED(svcQueryMemory(&info, &pg, addr))) break;
        u32 next = info.base_addr + info.size;
        if (info.size == 0) break;
        // any readable+writable region (skip free/IO/reserved). This covers the
        // game's .data/.bss and heaps, whatever their exact MemState. Read-only
        // shared blocks (font, hid shmem) are excluded by the WRITE requirement.
        if ((info.perm & MEMPERM_READ) && (info.perm & MEMPERM_WRITE) &&
            info.state != MEMSTATE_FREE && info.state != MEMSTATE_IO &&
            info.state != MEMSTATE_RESERVED)
        {
            u32 b = info.base_addr, e = info.base_addr + info.size;
            if (b < lo) b = lo;   // clamp to the selected region window
            if (e > hi) e = hi;
            if (e > b) cb(b, e - b, ud);
        }
        if (next <= addr) break;
        addr = next;
    }
}

static u32 ReadN(u32 a, int w)
{
    if (w == 1) return R8(a);
    if (w == 2) return R16(a);
    return R32(a);
}
static void WriteN(u32 a, int w, u32 v)
{
    if (w == 1) W8(a, (u8)v);
    else if (w == 2) W16(a, (u16)v);
    else W32(a, v);
}

// ---- Cheat Search (CTRPF-style: results table on top, form on bottom) ----
typedef struct { u32 addr, newv, oldv; } Cand;
static Cand *g_cands;
static u32   g_candCap, g_candCount;
static int   g_searchStarted, g_searchWidth = 4; // bytes: 1/2/4
static int   g_scanType = 0;   // 0 =  1 >  2 <  3 changed  4 unchanged  5 increased  6 decreased
static int   g_step, g_capped;
static u32   g_searchValue;
static u32   g_searchSelAddr;  // address under the cursor (shared with RAM Dumper)
static int   g_searchType = 0; // 0 = Known Value, 1 = Unknown Search
static int   g_memRegion  = 0; // region preset index

// Unknown Search: raw snapshot of the scanned region(s). We compare live memory
// against this later, so a value we never knew can still be tracked by how it
// changes. Bytes (not per-address candidates) keeps it compact enough to fit.
#define SNAP_CAP (2u * 1024 * 1024)
static u8   *g_snap;             // lazily allocated, persists for the session
static u32   g_snapUsed;
typedef struct { u32 base, size, off; } SnapReg;
static SnapReg g_snapReg[64];
static int   g_snapRegN;
static int   g_unknownArmed;     // snapshot taken, awaiting first comparison scan

// One-step Undo: backup of the candidate list before the last scan.
static Cand *g_undo;
static u32   g_undoCount;
static int   g_undoStep, g_undoArmed, g_undoStarted, g_undoValid;

static const char *SCAN_NAME[7] = {
    "Equal To", "Greater Than", "Less Than", "Changed", "Unchanged", "Increased", "Decreased"
};
static const char *SEARCHTYPE_NAME[2] = { "Known Value", "Unknown Search" };
#define NUM_REGIONS 4
static const char *REGION_NAME[NUM_REGIONS] = {
    "All Memory", "Low 1-128M", "Mid 128-512M", "High 512M-1G"
};
static void RegionBounds(int r, u32 *lo, u32 *hi)
{
    switch (r) {
        case 1: *lo = 0x00100000; *hi = 0x08000000; break;
        case 2: *lo = 0x08000000; *hi = 0x20000000; break;
        case 3: *lo = 0x20000000; *hi = 0x40000000; break;
        default:*lo = 0x00100000; *hi = 0x40000000; break;
    }
}
static int ScanNeedsValue(int st) { return st <= 2; } // 0/1/2 use the value field

static u32 g_dbgRegions, g_dbgKB; // diagnostics: what the last scan visited

typedef struct { u32 value; } SeedCtx;
static void SeedCb(u32 base, u32 size, void *ud)
{
    g_dbgRegions++; g_dbgKB += size / 1024;
    u32 v = ((SeedCtx *)ud)->value; int w = g_searchWidth;
    for (u32 a = base; a + w <= base + size; a += w)
    {
        if (ReadN(a, w) != v) continue;
        if (g_candCount >= g_candCap) { g_capped = 1; return; }
        g_cands[g_candCount].addr = a;
        g_cands[g_candCount].newv = v;
        g_cands[g_candCount].oldv = v;
        g_candCount++;
    }
}
static void SearchSeed(u32 v)
{
    g_candCount = 0; g_capped = 0; g_dbgRegions = 0; g_dbgKB = 0;
    SeedCtx c = { v };
    ForEachRWRegion(SeedCb, &c);
    g_searchStarted = 1; g_step = 1;
}
static int MatchScan(int st, u32 nv, u32 ov, u32 val)
{
    switch (st)
    {
        case 0: return nv == val;  case 1: return nv > val;  case 2: return nv < val;
        case 3: return nv != ov;   case 4: return nv == ov;
        case 5: return nv > ov;    default: return nv < ov;
    }
}
static void SearchNext(u32 val)
{
    int w = g_searchWidth; u32 keep = 0;
    for (u32 i = 0; i < g_candCount; ++i)
    {
        u32 nv = ReadN(g_cands[i].addr, w), ov = g_cands[i].newv;
        if (MatchScan(g_scanType, nv, ov, val))
        {
            g_cands[keep].addr = g_cands[i].addr;
            g_cands[keep].newv = nv; g_cands[keep].oldv = ov; keep++;
        }
    }
    g_candCount = keep; g_step++;
}

// ---- Unknown Search: raw snapshot, compared on the next scan ----
static void SnapCb(u32 base, u32 size, void *ud)
{
    (void)ud;
    if (!g_snap || g_snapRegN >= 64 || g_snapUsed >= SNAP_CAP) { g_capped = 1; return; }
    u32 room = SNAP_CAP - g_snapUsed;
    u32 n = size; if (n > room) { n = room; g_capped = 1; }
    memcpy(g_snap + g_snapUsed, (const void *)base, n);
    g_snapReg[g_snapRegN].base = base;
    g_snapReg[g_snapRegN].size = n;
    g_snapReg[g_snapRegN].off  = g_snapUsed;
    g_snapRegN++;
    g_snapUsed += n;
}
static void SnapshotArm(void)
{
    g_candCount = 0; g_capped = 0; g_snapUsed = 0; g_snapRegN = 0;
    if (!g_snap) { QueueToastRaw("No snapshot buffer", ""); return; }
    ForEachRWRegion(SnapCb, NULL);
    g_searchStarted = 1; g_unknownArmed = 1; g_step = 1;
}
static u32 SnapN(u32 off, int w)
{
    u8 *p = g_snap + off;
    if (w == 1) return p[0];
    if (w == 2) return (u32)p[0] | ((u32)p[1] << 8);
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
// First comparison after a snapshot: build candidates where live memory relates
// to the snapshot the way the scan type says (Changed / Increased / Decreased...).
static void SnapshotMaterialize(u32 val)
{
    int w = g_searchWidth; g_candCount = 0; g_capped = 0;
    for (int r = 0; r < g_snapRegN; ++r)
    {
        u32 base = g_snapReg[r].base, sz = g_snapReg[r].size, off = g_snapReg[r].off;
        for (u32 a = base; a + w <= base + sz; a += w)
        {
            u32 nv = ReadN(a, w);
            u32 ov = SnapN(off + (a - base), w);
            if (!MatchScan(g_scanType, nv, ov, val)) continue;
            if (g_candCount >= g_candCap) { g_capped = 1; break; }
            g_cands[g_candCount].addr = a;
            g_cands[g_candCount].newv = nv;
            g_cands[g_candCount].oldv = ov;
            g_candCount++;
        }
        if (g_capped) break;
    }
    g_unknownArmed = 0; g_step++;
}

// ---- one-step Undo ----
static void SaveUndo(void)
{
    if (!g_undo) return;
    memcpy(g_undo, g_cands, g_candCount * sizeof(Cand));
    g_undoCount = g_candCount; g_undoStep = g_step;
    g_undoArmed = g_unknownArmed; g_undoStarted = g_searchStarted;
    g_undoValid = 1;
}
static void DoUndo(void)
{
    if (!g_undoValid || !g_undo) { QueueToastRaw("Nothing to undo", ""); return; }
    memcpy(g_cands, g_undo, g_undoCount * sizeof(Cand));
    g_candCount = g_undoCount; g_step = g_undoStep;
    g_unknownArmed = g_undoArmed; g_searchStarted = g_undoStarted;
    g_undoValid = 0; g_capped = 0;
    QueueToastRaw("Undo", ": last scan reverted");
}

// perform a search step using the current form state
static void DoSearch(void)
{
    if (!g_candCap) { QueueToastRaw("No memory", ""); return; }
    RegionBounds(g_memRegion, &g_scanLo, &g_scanHi); // apply the region window
    SaveUndo();
    if (!g_searchStarted)
    {
        if (g_searchType == 1) SnapshotArm();            // Unknown: capture snapshot
        else                   SearchSeed(g_searchValue);// Known: seed by value
    }
    else if (g_unknownArmed) SnapshotMaterialize(g_searchValue); // first compare vs snapshot
    else                     SearchNext(g_searchValue);          // subsequent filters
}

// --- top screen: results table (Address / New / Old) with a selection cursor ---
#define SR_ROWS 9   // visible result rows
static void SearchDrawResults(int scroll, int cursor)
{
    ComposeBackdrop();
    CText(WIN_X + 12, WIN_Y + 6, T("Cheat Search"), GOLD, 1);
    char hit[48];
    if (!g_searchStarted) sniprintf(hit, sizeof hit, "%s", T("no search"));
    else if (g_unknownArmed) sniprintf(hit, sizeof hit, "Snapshot %luKB%s", (unsigned long)(g_snapUsed / 1024), g_capped ? "+" : "");
    else sniprintf(hit, sizeof hit, "Step %d   Hits: %lu%s", g_step, (unsigned long)g_candCount, g_capped ? "+" : "");
    CText6(WIN_X + WIN_W - 12 - C6Width(hit), WIN_Y + 9, hit, g_capped ? 233 : 196, g_capped ? 115 : 180, g_capped ? 107 : 150);
    CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);

    int cx0 = WIN_X + 16, cx1 = WIN_X + 140, cx2 = WIN_X + 244;
    CText6(cx0, WIN_Y + 26, T("Address"), INK_DIM);
    CText6(cx1, WIN_Y + 26, T("New Value"), INK_DIM);
    CText6(cx2, WIN_Y + 26, T("Old Value"), INK_DIM);

    int rowY = WIN_Y + 40, rh = 15, w = g_searchWidth;
    for (int i = scroll; i < (int)g_candCount && i < scroll + SR_ROWS; ++i)
    {
        int y = rowY + (i - scroll) * rh;
        if (i == cursor)
        {
            CFillBlend(WIN_X + 12, y - 1, WIN_W - 24, rh, 0, 0, 0, 120);
            CFill(WIN_X + 12, y - 1, 2, rh, GOLD);
        }
        char a[12], nv[14], ov[14];
        siprintf(a, "%08lX", (unsigned long)g_cands[i].addr);
        siprintf(nv, "%lu", (unsigned long)ReadN(g_cands[i].addr, w)); // New = live value
        siprintf(ov, "%lu", (unsigned long)g_cands[i].newv);           // Old = value at last scan (the Next baseline)
        CText6(cx0, y, a, (i == cursor) ? 246 : 236, (i == cursor) ? 236 : 224, (i == cursor) ? 200 : 198);
        CText6(cx1, y, nv, GREEN_ON);
        CText6(cx2, y, ov, INK_DIM);
    }
    if (!g_candCount)
    {
        if (g_unknownArmed)
        {
            CText6(cx0, rowY,      T("Snapshot taken."), GREEN_ON);
            CText6(cx0, rowY + 16, T("1. Change the value in the game."), INK_DIM);
            CText6(cx0, rowY + 29, T("2. Pick Changed / Decreased / etc."), INK_DIM);
            CText6(cx0, rowY + 42, T("3. Search. Repeat to narrow down."), INK_DIM);
        }
        else
            CText6(cx0, rowY, g_searchStarted ? T("(no matches)") : T("Set the form below, then Search."), INK_DIM);
    }

    // scroll arrows (right edge)
    if (scroll > 0)
        for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, rowY + 2 + a, 1 + 2 * a, 1, GOLD);
    if (scroll + SR_ROWS < (int)g_candCount)
        for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, rowY + SR_ROWS * rh - 4 - a, 1 + 2 * a, 1, GOLD);

    // footer: controls (left) + page indicator (right), guarded against overlap
    if (g_candCount)
    {
        const char *leg = T("{A} poke  {DP} move  {B} exit");
        CText6(WIN_X + 12, WIN_Y + WIN_H - 14, leg, INK_DIM);
        char pg[28];
        siprintf(pg, "%d / %lu", cursor + 1, (unsigned long)g_candCount);
        int pgX = WIN_X + WIN_W - 12 - C6Width(pg);
        int legEnd = WIN_X + 12 + C6Width(leg);
        if (pgX > legEnd + 8) // only draw if it clears the legend
            CText6(pgX, WIN_Y + WIN_H - 14, pg, INK_DIM);
    }
    else
        CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{X} type  {Y} scan  {R} search  {B} exit"), INK_DIM);
}

// --- bottom screen: touch form ---
// codes: 1 valtype  2 scan  3 value  4 search  5 reset  6 region  7 searchtype  8 undo
typedef struct { int x, y, w, h, code; } FBox;
#define SF_VX  120
#define SF_VW  186
#define SF_FY  36
#define SF_FH  21
#define SF_G   4
static int SearchBuildForm(FBox *f)
{
    int n = 0;
    static const int codes[5] = { 6, 7, 1, 2, 3 }; // region, search type, value type, scan, value
    for (int i = 0; i < 5; ++i)
        f[n++] = (FBox){ SF_VX, SF_FY + i * (SF_FH + SF_G), SF_VW, SF_FH, codes[i] };
    int by = SF_FY + 5 * (SF_FH + SF_G) + 8;
    f[n++] = (FBox){ 14,  by, 93, 28, 4 };  // Search
    f[n++] = (FBox){ 113, by, 93, 28, 8 };  // Undo
    f[n++] = (FBox){ 212, by, 94, 28, 5 };  // Reset
    return n;
}
static void SearchDrawForm(void)
{
    // raw frozen game frame...
    for (int y = 0; y < BOT_H; ++y)
        for (int x = 0; x < BOT_W; ++x)
        {
            u8 *p = CPix(x, y);
            if (savedBotValid)
            { u16 v = savedBot[y * BOT_W + x];
              p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
            else { p[0] = p[1] = p[2] = 12; }
        }
    // ...covered by a solid dark-brown panel so fields are always readable
    CFillBlend(0, 0, BOT_W, BOT_H, BG, 230); // ~80% opaque
    CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);

    CText(14, 8, T("Cheat Search"), GOLD, 1);
    CFill(14, 30, C6Width(T("Cheat Search")) * 2, 1, GOLD);

    int lx = 14, vx = SF_VX, vw = SF_VW, fy = SF_FY, fh = SF_FH, g = SF_G;
    const char *labels[5] = { "Memory Region", "Search Type", "Value Type", "Scan Type", "Value" };
    int locked = g_searchStarted; // region / type / width are fixed once a search starts
    int dimValue = (!ScanNeedsValue(g_scanType)) || (g_searchType == 1 && !g_searchStarted);
    int dims[5] = { locked, locked, locked, 0, dimValue };
    char val[5][40];
    sniprintf(val[0], sizeof val[0], "%s", T(REGION_NAME[g_memRegion]));
    sniprintf(val[1], sizeof val[1], "%s", T(SEARCHTYPE_NAME[g_searchType]));
    siprintf(val[2], "%d Bytes  (%d-bit)", g_searchWidth, g_searchWidth * 8);
    sniprintf(val[3], sizeof val[3], "%s", T(SCAN_NAME[g_scanType]));
    if (g_searchType == 1 && !g_searchStarted) sniprintf(val[4], sizeof val[4], "%s", T("(not needed)"));
    else if (dimValue)                         siprintf(val[4], "--");
    else siprintf(val[4], "%lu  (0x%lX)", (unsigned long)g_searchValue, (unsigned long)g_searchValue);

    for (int i = 0; i < 5; ++i)
    {
        int y = fy + i * (fh + g);
        CText6(lx, y + 4, T(labels[i]), INK);
        int dim = dims[i];
        CFill(vx, y, vw, fh, dim ? 26 : 52, dim ? 20 : 40, dim ? 12 : 22);
        CFill(vx, y, vw, 1, GOLD); CFill(vx, y + fh - 1, vw, 1, GOLD);
        CFill(vx, y, 1, fh, GOLD); CFill(vx + vw - 1, y, 1, fh, GOLD);
        CText6(vx + 6, y + 4, val[i], dim ? 120 : 236, dim ? 116 : 236, dim ? 96 : 210);
    }
    int by = fy + 5 * (fh + g) + 8;
    KbKey(14,  by, 93, 28, g_searchStarted ? T("Next") : T("Search"), 5, 0);
    KbKey(113, by, 93, 28, T("Undo"), g_undoValid ? 4 : 3, 0);
    KbKey(212, by, 94, 28, T("Reset"), 6, 0);
    const char *lg1 = T("Tap a field, or:");
    const char *lg2 = T("{X} type   {Y} scan   {R} search   {L} undo");
    CText6((BOT_W - C6Width(lg1)) / 2, by + 31, lg1, INK_DIM);
    CText6Btn((BOT_W - C6BtnWidth(lg2)) / 2, by + 43, lg2, INK_DIM);
    BotBlitComposeBoth();
}

static void ToolSearch(void)
{
    KbInit(); // touch input for the form (was missing -> taps did nothing)
    if (!g_cands)
    {
        g_candCap = 0x8000; // 32768 * 12B = 384KB
        g_cands = (Cand *)malloc(g_candCap * sizeof(Cand));
        if (!g_cands) g_candCap = 0;
    }
    if (!g_undo && g_candCap)                       // one-step undo backup (384KB)
        g_undo = (Cand *)malloc(g_candCap * sizeof(Cand));
    if (!g_snap)                                    // Unknown Search snapshot (2MB)
        g_snap = (u8 *)malloc(SNAP_CAP);

    int scroll = 0, cursor = 0, redrawTop = 1, redrawBot = 1;
    u32 prev = HID_PAD;
    int touchPrev = HidTouch(0, 0);

    while (1)
    {
        if (cursor >= (int)g_candCount) cursor = g_candCount ? g_candCount - 1 : 0;
        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + SR_ROWS) scroll = cursor - SR_ROWS + 1;
        if (g_candCount) g_searchSelAddr = g_cands[cursor].addr; // shared with RAM Dumper

        if (redrawTop) { SearchDrawResults(scroll, cursor); Present(); Present(); redrawTop = 0; }
        if (redrawBot) { SearchDrawForm(); redrawBot = 0; }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if ((down & BUTTON_DOWN) && cursor + 1 < (int)g_candCount) { cursor++; redrawTop = 1; }
        if ((down & BUTTON_UP)   && cursor > 0)                    { cursor--; redrawTop = 1; }
        if (down & BUTTON_B) break;
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; } // straight to game, results kept

        // physical shortcuts: X = value type, Y = scan type, R = Search
        if (down & BUTTON_X)
        {
            if (!g_searchStarted) g_searchWidth = (g_searchWidth == 1) ? 2 : (g_searchWidth == 2) ? 4 : 1;
            else QueueToastRaw("Reset to change width", "");
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_Y)
        {
            g_scanType = (g_scanType + 1) % 7;
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_L1) // L = undo last scan
        {
            DoUndo(); scroll = 0; cursor = 0;
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_R1)
        {
            ComposeBackdrop(); CText(WIN_X + 110, WIN_Y + 90, T("Scanning..."), GOLD, 1); Present(); Present();
            DoSearch(); scroll = 0; cursor = 0;
            redrawTop = 1; redrawBot = 1;
            prev = HID_PAD;
            continue;
        }

        // A = poke the selected result
        if ((down & BUTTON_A) && g_candCount)
        {
            int cancel;
            u32 addr = g_cands[cursor].addr;
            u32 nv = EnterNum("Poke value", ReadN(addr, g_searchWidth), &cancel);
            if (!cancel) { WriteN(addr, g_searchWidth, nv); g_cands[cursor].newv = nv; QueueToastRaw("Poked", ""); }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            continue;
        }

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;
        FBox f[8]; int nf = SearchBuildForm(f);
        for (int i = 0; i < nf; ++i)
        {
            if (!KbHit(px, py, f[i].x, f[i].y, f[i].w, f[i].h)) continue;
            switch (f[i].code)
            {
                case 1: // value type (only before first search)
                    if (!g_searchStarted)
                        g_searchWidth = (g_searchWidth == 1) ? 2 : (g_searchWidth == 2) ? 4 : 1;
                    else QueueToastRaw("Reset to change width", "");
                    break;
                case 2: g_scanType = (g_scanType + 1) % 7; break;
                case 3: // value entry
                {
                    int cancel;
                    u32 v = EnterNum("Search value", g_searchValue, &cancel);
                    if (!cancel) g_searchValue = v;
                    break;
                }
                case 4: // Search
                    ComposeBackdrop(); CText(WIN_X + 110, WIN_Y + 90, T("Scanning..."), GOLD, 1); Present(); Present();
                    DoSearch(); scroll = 0; cursor = 0;
                    break;
                case 5: // Reset (clears results, snapshot and undo; keeps your form choices)
                    g_candCount = 0; g_searchStarted = 0; g_capped = 0; g_step = 0;
                    g_unknownArmed = 0; g_snapUsed = 0; g_snapRegN = 0; g_undoValid = 0;
                    scroll = 0; cursor = 0;
                    break;
                case 6: // memory region (only before a search starts)
                    if (!g_searchStarted) g_memRegion = (g_memRegion + 1) % NUM_REGIONS;
                    else QueueToastRaw("Reset to change region", "");
                    break;
                case 7: // search type: Known / Unknown (only before a search starts)
                    if (!g_searchStarted) g_searchType = (g_searchType + 1) % 2;
                    else QueueToastRaw("Reset to change type", "");
                    break;
                case 8: // Undo
                    DoUndo(); scroll = 0; cursor = 0;
                    break;
            }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            break;
        }
    }

    // Search state PERSISTS across tool exits and full menu close/reopen.
    // This is what enables the real cheat-search loop: seed a value -> exit menu
    // (unpauses the game) -> change the value in-game -> reopen -> filter by
    // Changed / Increased / Decreased / equals. The g_cands buffer stays
    // allocated for the plugin lifetime; only the "Reset" button clears results.
}

// ---- About (scrollable credits) ----
static void ToolAbout(void)
{
    static const struct { const char *s; int gold; } lines[] = {
        { "OcarinaCTRComposer  " PLUGIN_VER,            1 },
        { "",                                           0 },
        { "Made by samabr",                             1 },
        { "github.com/samaBR85/OcarinaCTRComposer",     0 },
        { "",                                           0 },
        { "Cheats & codes: Nanquitas / Fort42",         0 },
        { "Item icons: Spriters Resource",              0 },
        { "   (ripped by Colbydude)",                   0 },
        { "Buttons/logo: Spriters Resource (manpaint)", 0 },
        { "Loader: Luma3DS (LumaTeam)",                 0 },
        { "3GX loader / tool: PabloMK7",                0 },
        { "Walkthrough: z64central.com",                0 },
        { "   (packaged by LowEndC)",                   0 },
        { "Save-data map: HelpTheWretched",             0 },
        { "Teleport refs: gamestabled &",               0 },
        { "   HylianFreddy (practice menus)",           0 },
        { "",                                           0 },
        { "The Legend of Zelda (c) Nintendo.",          0 },
        { "Fan project, non-commercial.",               0 },
    };
    int N = (int)(sizeof(lines) / sizeof(lines[0]));
    int x = WIN_X + 16;
    int top = WIN_Y + 10 + LOGO_H + 10;   // text block starts below the fixed logo
    int footY = WIN_Y + WIN_H - 16;
    int vis = (footY - top - 4) / 12;      // lines that fit in the scroll area
    int scroll = 0, redraw = 1;
    u32 prev = HID_PAD;
    while (1)
    {
        if (redraw)
        {
            ComposeBackdrop();
            DrawImg(WIN_X + (WIN_W - LOGO_W) / 2, WIN_Y + 10, logoPx, LOGO_W, LOGO_H);
            for (int i = 0; i < vis && scroll + i < N; ++i)
            {
                const char *s = lines[scroll + i].s;
                if (s[0]) CText6(x, top + i * 12, s, RGB3(lines[scroll + i].gold ? CGOLD : CDIM));
            }
            if (scroll > 0)                          // up arrow (more above)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, top + 3 + a, 1 + 2*a, 1, GOLD);
            if (scroll + vis < N)                    // down arrow (more below)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, footY - 6 - a, 1 + 2*a, 1, GOLD);
            CText6Btn(x, footY, "{DP} scroll    {B} back", INK_DIM);
            Present(); Present();
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if ((down & BUTTON_DOWN) && scroll + vis < N) { scroll++; redraw = 1; }
        if ((down & BUTTON_UP)   && scroll > 0)       { scroll--; redraw = 1; }
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; }
        if (down & (BUTTON_B | BUTTON_A)) break;
    }
    DrainButtons(~0u); // capped: a stuck pad must not hang the console with the game paused
}

// ---- RAM Dumper ----
static u32 g_dumpStart = 0x08000000;
static int g_dumpSizeIdx = 3;
static const u32   DUMP_SIZES[6]   = { 0x1000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000 };
static const char *DUMP_SIZE_NM[6] = { "4 KB", "64 KB", "256 KB", "1 MB", "4 MB", "16 MB" };
#define NUM_DUMP_SIZES 6
#define DUMP_DIR "/luma/plugins/0004000000033500/dumps"

// How many contiguous readable bytes exist from `start`, so a dump never faults
// on an unmapped hole. Returns bytes readable (<= want), or 0 if start is dead.
static u32 ReadableSpan(u32 start, u32 want)
{
    u32 end = start + want, a = start;
    while (a < end)
    {
        MemInfo info; PageInfo pg;
        if (R_FAILED(svcQueryMemory(&info, &pg, a))) break;
        u32 rend = info.base_addr + info.size;
        if (!(info.perm & MEMPERM_READ) || info.state == MEMSTATE_FREE) break;
        if (rend >= end) return want;
        if (rend <= a) break;
        a = rend;
    }
    return a > start ? a - start : 0;
}

static void RamDumpDrawTop(const char *status, u8 sr, u8 sg, u8 sb, int pct)
{
    ComposeBackdrop();
    CText(WIN_X + 12, WIN_Y + 6, T("RAM Dumper"), GOLD, 1);
    CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);
    u32 size = DUMP_SIZES[g_dumpSizeIdx];
    int x = WIN_X + 16, y = WIN_Y + 30; char l[72];
    sniprintf(l, sizeof l, "%s  0x%08lX", T("Start:"), (unsigned long)g_dumpStart);          CText6(x, y, l, INK); y += 15;
    sniprintf(l, sizeof l, "%s  0x%08lX", T("End:"), (unsigned long)(g_dumpStart + size)); CText6(x, y, l, INK); y += 15;
    sniprintf(l, sizeof l, "%s  %s", T("Size:"), DUMP_SIZE_NM[g_dumpSizeIdx]);              CText6(x, y, l, INK); y += 19;
    CText6(x, y, T("Saves to:"), INK_DIM); y += 13;
    CText6(x, y, DUMP_DIR "/", INK_DIM); y += 13;
    CText6(x, y, "  dump_<start>_<size>.bin", INK_DIM); y += 19;
    if (status) CText6(x, y, T(status), sr, sg, sb);
    y += 16;
    if (pct >= 0)
    {
        int bw = WIN_W - 40, bx = x;
        CFill(bx, y, bw, 9, 40, 32, 20);
        CFill(bx, y, bw * pct / 100, 9, 120, 200, 120);
        CFill(bx, y, bw, 1, GOLD); CFill(bx, y + 8, bw, 1, GOLD);
        CFill(bx, y, 1, 9, GOLD);  CFill(bx + bw - 1, y, 1, 9, GOLD);
    }
    CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{X} size  {Y} from-search  {R} dump  {B} exit"), INK_DIM);
    Present(); Present();
}

static void RamDumpDrawForm(void)
{
    for (int yy = 0; yy < BOT_H; ++yy)
        for (int xx = 0; xx < BOT_W; ++xx)
        {
            u8 *p = CPix(xx, yy);
            if (savedBotValid)
            { u16 v = savedBot[yy * BOT_W + xx];
              p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
            else { p[0] = p[1] = p[2] = 12; }
        }
    CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
    CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
    CText(14, 8, T("RAM Dumper"), GOLD, 1);
    CFill(14, 30, C6Width(T("RAM Dumper")) * 2, 1, GOLD);

    int lx = 14, vx = 120, vw = 186, fy = 66, fh = 26, g = 12; // fy centers the form block in the lower area
    const char *labels[2] = { "Start Addr", "Size" };
    char val[2][40];
    siprintf(val[0], "0x%08lX", (unsigned long)g_dumpStart);
    sniprintf(val[1], sizeof val[1], "%s", DUMP_SIZE_NM[g_dumpSizeIdx]);
    for (int i = 0; i < 2; ++i)
    {
        int y = fy + i * (fh + g);
        CText6(lx, y + 6, T(labels[i]), INK);
        CFill(vx, y, vw, fh, 52, 40, 22);
        CFill(vx, y, vw, 1, GOLD); CFill(vx, y + fh - 1, vw, 1, GOLD);
        CFill(vx, y, 1, fh, GOLD); CFill(vx + vw - 1, y, 1, fh, GOLD);
        CText6(vx + 6, y + 6, val[i], 236, 236, 210);
    }
    int by = fy + 2 * (fh + g) + 10;
    KbKey(14,  by, 140, 32, T("From Search"), 4, 0);
    KbKey(166, by, 140, 32, T("Dump"), 5, 0);
    const char *hint = T("Tap a field, or use the buttons");
    CText6((BOT_W - C6Width(hint)) / 2, by + 38, hint, INK_DIM);
    BotBlitComposeBoth();
}

typedef struct { int x, y, w, h, code; } FBox2;
static int RamDumpBuildForm(FBox2 *f)
{
    int n = 0, vx = 120, vw = 186, fy = 66, fh = 26, g = 12; // fy must match ComposeRamDump above
    f[n++] = (FBox2){ vx, fy + 0 * (fh + g), vw, fh, 1 }; // start
    f[n++] = (FBox2){ vx, fy + 1 * (fh + g), vw, fh, 2 }; // size
    int by = fy + 2 * (fh + g) + 10;
    f[n++] = (FBox2){ 14,  by, 140, 32, 3 };              // From Search
    f[n++] = (FBox2){ 166, by, 140, 32, 4 };              // Dump
    return n;
}

static void DoDump(const char **status, u8 *sr, u8 *sg, u8 *sb)
{
    u32 want = DUMP_SIZES[g_dumpSizeIdx];
    u32 span = ReadableSpan(g_dumpStart, want);
    if (!span)      { *status = "Address not readable - try another Start"; *sr=236; *sg=140; *sb=120; return; }
    FsBootInit();
    if (!fsReady)   { *status = "SD card not available"; *sr=236; *sg=140; *sb=120; return; }
    FSUSER_CreateDirectory(cfgArchive, fsMakePath(PATH_ASCII, DUMP_DIR), 0); // ok if it exists
    char path[100];
    sniprintf(path, sizeof path, "%s/dump_%08lX_%luK.bin", DUMP_DIR,
             (unsigned long)g_dumpStart, (unsigned long)(span / 1024));
    Handle fh;
    if (R_FAILED(FSUSER_OpenFile(&fh, cfgArchive, fsMakePath(PATH_ASCII, path),
                                 FS_OPEN_WRITE | FS_OPEN_CREATE, 0)))
    { *status = "Could not create file"; *sr=236; *sg=140; *sb=120; return; }
    FSFILE_SetSize(fh, span);
    u32 off = 0, chunk = 0x40000; Result r = 0; int lastPct = -1;
    while (off < span)
    {
        u32 n = span - off; if (n > chunk) n = chunk;
        u32 wrote = 0;
        r = FSFILE_Write(fh, &wrote, off, (const void *)(g_dumpStart + off), n, 0);
        if (R_FAILED(r)) break;
        off += n;
        int pct = (int)((u64)off * 100 / span);
        if (pct != lastPct) { lastPct = pct; RamDumpDrawTop("Dumping...", 236, 200, 120, pct); }
    }
    FSFILE_Close(fh);
    if (R_FAILED(r)) { *status = "Write error"; *sr=236; *sg=140; *sb=120; return; }
    static char msg[80];
    if (span < want) siprintf(msg, "Saved %luKB (clamped to readable memory)", (unsigned long)(span / 1024));
    else             siprintf(msg, "Saved %luKB to dumps/dump_%08lX_%luK.bin",
                              (unsigned long)(span / 1024), (unsigned long)g_dumpStart, (unsigned long)(span / 1024));
    *status = msg; *sr=140; *sg=236; *sb=120;
    QueueToastRaw("RAM dumped", "");
}

static void ToolRamDump(void)
{
    KbInit();
    if (g_dumpStart == 0x08000000 && g_searchSelAddr) g_dumpStart = g_searchSelAddr; // handy default
    int redrawTop = 1, redrawBot = 1;
    u32 prev = HID_PAD; int touchPrev = HidTouch(0, 0);
    const char *status = NULL; u8 sr = 140, sg = 236, sb = 120;

    while (1)
    {
        if (redrawTop) { RamDumpDrawTop(status, sr, sg, sb, -1); redrawTop = 0; }
        if (redrawBot) { RamDumpDrawForm(); redrawBot = 0; }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if (down & BUTTON_B) break;
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; }
        if (down & BUTTON_X) { g_dumpSizeIdx = (g_dumpSizeIdx + 1) % NUM_DUMP_SIZES; redrawTop = 1; redrawBot = 1; }
        if (down & BUTTON_Y)
        {
            if (g_searchSelAddr) { g_dumpStart = g_searchSelAddr; status = "Loaded address from Cheat Search"; sr=236; sg=200; sb=120; }
            else                 { status = "No Cheat Search result yet"; sr=236; sg=180; sb=120; }
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_R1) { DoDump(&status, &sr, &sg, &sb); redrawTop = 1; redrawBot = 1; prev = HID_PAD; continue; }

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;
        FBox2 f[6]; int nf = RamDumpBuildForm(f);
        for (int i = 0; i < nf; ++i)
        {
            if (!KbHit(px, py, f[i].x, f[i].y, f[i].w, f[i].h)) continue;
            switch (f[i].code)
            {
                case 1: { int c; u32 v = EnterNum("Start address", g_dumpStart, &c); if (!c) g_dumpStart = v; } break;
                case 2: g_dumpSizeIdx = (g_dumpSizeIdx + 1) % NUM_DUMP_SIZES; break;
                case 3:
                    if (g_searchSelAddr) { g_dumpStart = g_searchSelAddr; status = "Loaded address from Cheat Search"; sr=236; sg=200; sb=120; }
                    else                 { status = "No Cheat Search result yet"; sr=236; sg=180; sb=120; }
                    break;
                case 4: DoDump(&status, &sr, &sg, &sb); break;
            }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            break;
        }
    }
}

// ---- Hex Editor ----
#define HEX_COLS 8
#define HEX_ROWS 11
#define HEX_LO   0x00100000u
#define HEX_HI   0x40000000u
static u32 g_hexCursor = 0x08000000;
static u32 g_hexView   = 0x08000000;

// cached readable window so a page of bytes costs ~1 svcQueryMemory, not 88.
static u32 g_mrLo = 1, g_mrHi = 0; static int g_mrOk = 0;
static int MemReadable(u32 a)
{
    if (a >= g_mrLo && a < g_mrHi) return g_mrOk;
    MemInfo info; PageInfo pg;
    if (R_FAILED(svcQueryMemory(&info, &pg, a))) { g_mrLo = a & ~0xFFFu; g_mrHi = g_mrLo + 0x1000; g_mrOk = 0; return 0; }
    g_mrLo = info.base_addr; g_mrHi = info.base_addr + info.size;
    g_mrOk = (info.perm & MEMPERM_READ) && info.state != MEMSTATE_FREE;
    return g_mrOk;
}
static int MemWritable(u32 a)
{
    MemInfo info; PageInfo pg;
    if (R_FAILED(svcQueryMemory(&info, &pg, a))) return 0;
    return (info.perm & MEMPERM_WRITE) && info.state != MEMSTATE_FREE;
}

static void HexClampCursor(void)
{
    if (g_hexCursor < HEX_LO) g_hexCursor = HEX_LO;
    if (g_hexCursor > HEX_HI - 1) g_hexCursor = HEX_HI - 1;
    u32 span = HEX_ROWS * HEX_COLS;
    u32 curRow = g_hexCursor - (g_hexCursor % HEX_COLS);
    if (g_hexCursor < g_hexView) g_hexView = curRow;
    else if (g_hexCursor >= g_hexView + span) g_hexView = curRow - (span - HEX_COLS);
    if (g_hexView < HEX_LO) g_hexView = HEX_LO;
}

static void HexDrawTop(void)
{
    ComposeBackdrop();
    CText(WIN_X + 12, WIN_Y + 6, T("Hex Editor"), GOLD, 1);
    char h[32];
    int rd = MemReadable(g_hexCursor);
    siprintf(h, "%08lX = %02X", (unsigned long)g_hexCursor, rd ? R8(g_hexCursor) : 0);
    CText6(WIN_X + WIN_W - 12 - C6Width(h), WIN_Y + 9, h, rd ? 236 : 200, rd ? 236 : 150, rd ? 210 : 120);
    CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);

    int ax = WIN_X + 12, hx0 = ax + 62, asc0 = hx0 + HEX_COLS * 20 + 6;
    for (int r = 0; r < HEX_ROWS; ++r)
    {
        u32 rowAddr = g_hexView + (u32)r * HEX_COLS;
        int y = WIN_Y + 28 + r * 13;
        char al[10]; siprintf(al, "%08lX", (unsigned long)rowAddr);
        CText6(ax, y, al, INK_DIM);
        for (int c = 0; c < HEX_COLS; ++c)
        {
            u32 a = rowAddr + c;
            int rdb = MemReadable(a);
            int bx = hx0 + c * 20;
            if (a == g_hexCursor)
            {
                CFillBlend(bx - 2, y - 1, 17, 12, 0, 0, 0, 150);
                CFill(bx - 2, y - 1, 17, 1, GOLD); CFill(bx - 2, y + 10, 17, 1, GOLD);
                CFill(bx - 2, y - 1, 1, 12, GOLD); CFill(bx + 14, y - 1, 1, 12, GOLD);
            }
            char bb[4]; if (rdb) siprintf(bb, "%02X", R8(a)); else siprintf(bb, "--");
            CText6(bx, y, bb, rdb ? 236 : 96, rdb ? 236 : 80, rdb ? 210 : 60);
            u8 v = rdb ? R8(a) : 0;
            char ch[2]; ch[0] = (v >= 32 && v < 127) ? (char)v : '.'; ch[1] = 0;
            CText6(asc0 + c * 7, y, ch, a == g_hexCursor ? 236 : 150, a == g_hexCursor ? 200 : 140, a == g_hexCursor ? 120 : 112);
        }
    }
    CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{DP} move  {A} edit  {X} goto  {B} exit"), INK_DIM);
    Present(); Present();
}

static void HexDrawForm(void)
{
    for (int yy = 0; yy < BOT_H; ++yy)
        for (int xx = 0; xx < BOT_W; ++xx)
        {
            u8 *p = CPix(xx, yy);
            if (savedBotValid)
            { u16 v = savedBot[yy * BOT_W + xx];
              p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
            else { p[0] = p[1] = p[2] = 12; }
        }
    CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
    CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
    CText(14, 8, T("Hex Editor"), GOLD, 1);
    CFill(14, 30, C6Width(T("Hex Editor")) * 2, 1, GOLD);

    int lx = 14, vx = 120, vw = 186, fy = 46, fh = 26, g = 12;
    const char *labels[2] = { "Address", "Byte @ cursor" };
    char val[2][40];
    siprintf(val[0], "0x%08lX", (unsigned long)g_hexCursor);
    if (MemReadable(g_hexCursor)) siprintf(val[1], "0x%02X  (%u)", R8(g_hexCursor), R8(g_hexCursor));
    else                          sniprintf(val[1], sizeof val[1], "%s", T("-- (unreadable)"));
    for (int i = 0; i < 2; ++i)
    {
        int y = fy + i * (fh + g);
        CText6(lx, y + 6, T(labels[i]), INK);
        CFill(vx, y, vw, fh, 52, 40, 22);
        CFill(vx, y, vw, 1, GOLD); CFill(vx, y + fh - 1, vw, 1, GOLD);
        CFill(vx, y, 1, fh, GOLD); CFill(vx + vw - 1, y, 1, fh, GOLD);
        CText6(vx + 6, y + 6, val[i], 236, 236, 210);
    }
    int by = fy + 2 * (fh + g) + 10;
    KbKey(14,  by, 140, 32, T("From Search"), 4, 0);
    KbKey(166, by, 140, 32, T("Edit Byte"), 5, 0);
    const char *hint = T("On top: {DP} move  {L}/{R} page  {Y} from-search");
    CText6Btn((BOT_W - C6BtnWidth(hint)) / 2, by + 38, hint, INK_DIM);
    BotBlitComposeBoth();
}

typedef struct { int x, y, w, h, code; } FBox3;
static int HexBuildForm(FBox3 *f)
{
    int n = 0, vx = 120, vw = 186, fy = 46, fh = 26, g = 12;
    f[n++] = (FBox3){ vx, fy + 0 * (fh + g), vw, fh, 1 }; // address (goto)
    f[n++] = (FBox3){ vx, fy + 1 * (fh + g), vw, fh, 2 }; // byte (edit)
    int by = fy + 2 * (fh + g) + 10;
    f[n++] = (FBox3){ 14,  by, 140, 32, 3 };              // From Search
    f[n++] = (FBox3){ 166, by, 140, 32, 4 };              // Edit Byte
    return n;
}

static void HexEditByte(void)
{
    if (!MemWritable(g_hexCursor)) { QueueToastRaw("Read-only here", ""); return; }
    int c; u32 v = EnterNum("Edit byte", MemReadable(g_hexCursor) ? R8(g_hexCursor) : 0, &c);
    if (!c) { W8(g_hexCursor, (u8)v); QueueToastRaw("Byte written", ""); }
}
static void HexGoto(void)
{
    int c; u32 a = EnterNum("Go to address", g_hexCursor, &c);
    if (!c) { g_hexCursor = a; HexClampCursor(); }
}

static void ToolHexEdit(void)
{
    KbInit();
    g_mrLo = 1; g_mrHi = 0; // reset readable cache
    if (g_searchSelAddr) g_hexCursor = g_searchSelAddr;   // start where the user was looking
    HexClampCursor(); g_hexView = g_hexCursor - (g_hexCursor % HEX_COLS);
    HexClampCursor();

    int redrawTop = 1, redrawBot = 1;
    u32 prev = HID_PAD; int touchPrev = HidTouch(0, 0);

    while (1)
    {
        if (redrawTop) { HexDrawTop(); redrawTop = 0; }
        if (redrawBot) { HexDrawForm(); redrawBot = 0; }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if (down & BUTTON_B) break;
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; }

        int moved = 0;
        if (down & BUTTON_LEFT)  { g_hexCursor -= 1; moved = 1; }
        if (down & BUTTON_RIGHT) { g_hexCursor += 1; moved = 1; }
        if (down & BUTTON_UP)    { g_hexCursor -= HEX_COLS; moved = 1; }
        if (down & BUTTON_DOWN)  { g_hexCursor += HEX_COLS; moved = 1; }
        if (down & BUTTON_L1)    { g_hexCursor -= HEX_ROWS * HEX_COLS; moved = 1; }
        if (down & BUTTON_R1)    { g_hexCursor += HEX_ROWS * HEX_COLS; moved = 1; }
        if (moved) { HexClampCursor(); redrawTop = 1; redrawBot = 1; }

        if (down & BUTTON_A) { HexEditByte(); redrawTop = 1; redrawBot = 1; prev = HID_PAD; continue; }
        if (down & BUTTON_X) { HexGoto();     redrawTop = 1; redrawBot = 1; prev = HID_PAD; continue; }
        if (down & BUTTON_Y)
        {
            if (g_searchSelAddr) { g_hexCursor = g_searchSelAddr; HexClampCursor(); }
            else QueueToastRaw("No Cheat Search result yet", "");
            redrawTop = 1; redrawBot = 1;
        }

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;
        FBox3 f[6]; int nf = HexBuildForm(f);
        for (int i = 0; i < nf; ++i)
        {
            if (!KbHit(px, py, f[i].x, f[i].y, f[i].w, f[i].h)) continue;
            switch (f[i].code)
            {
                case 1: HexGoto(); break;
                case 2: HexEditByte(); break;
                case 3:
                    if (g_searchSelAddr) { g_hexCursor = g_searchSelAddr; HexClampCursor(); }
                    else QueueToastRaw("No Cheat Search result yet", "");
                    break;
                case 4: HexEditByte(); break;
            }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            break;
        }
    }
}
