#define CHK_MAXITEMS 32
#define CHK_PATH "/luma/plugins/0004000000033500/Checklist.txt"

static u8  chkState[CHK_NCATS][CHK_MAXITEMS]; // 0 untouched, 1 auto, 2 you-checked, 3 you-cleared
static int chkLoaded = 0;

// ---- type-icons: 8x8 monochrome bitmaps, scaled to any pixel size (placeholders; real sprites
// can replace these later - see oot3d_items.png "QUEST STATUS SCREEN" once extracted) ----
static const u8 iconHeartBmp[8] = { 0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00 };
static const u8 iconSkullBmp[8] = { 0x24,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x42 };
static const u8 iconNoteBmp[8]  = { 0x0C,0x0C,0x0E,0x0C,0x0C,0x7C,0xFC,0x78 };
static const u8 iconKeyBmp[8]   = { 0x38,0x44,0x44,0x38,0x10,0x10,0x34,0x00 };
static void DrawBitmapIcon(const u8 *bmp, int x, int y, int cell, u8 r, u8 g, u8 b)
{
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
            if (bmp[row] & (0x80 >> col))
                CFill(x + col * cell, y + row * cell, cell, cell, r, g, b);
}
// cell=2 -> 16px (list rows, matches DrawSprite's small size); cell=5 -> 40px (detail card).
static void DrawChkIcon(const ChkItem *it, int x, int y, int cell)
{
    switch (it->iconKind)
    {
        case CKI_SPRITE:  if (FindSprite(it->iconArg)) DrawSprite(x, y, it->iconArg, cell >= 4); break;
        case CKI_HEART:   DrawBitmapIcon(iconHeartBmp, x, y, cell, 220, 60, 60); break;
        case CKI_SKULL:   DrawBitmapIcon(iconSkullBmp, x, y, cell, 224, 186, 96); break;
        case CKI_NOTE: {
            // Each ocarina song's note has a color (per the in-game song menu). iconArg selects it:
            // 0 = cyan (the 4 non-warp songs are all cyan) then the 6 warp songs by their color.
            static const u8 songCol[7][3] = {
                { 90,210,230}, // 0 cyan  - Zelda's Lullaby / Sun / Epona / Saria
                { 90,210, 90}, // 1 green - Minuet of Forest
                {230, 90, 90}, // 2 red   - Bolero of Fire
                { 96,128,235}, // 3 blue  - Serenade of Water
                {235,160, 70}, // 4 orange- Requiem of Spirit
                {184,102,222}, // 5 purple- Nocturne of Shadow
                {232,212, 84}, // 6 yellow- Prelude of Light
            };
            int ci = it->iconArg < 7 ? it->iconArg : 0;
            DrawBitmapIcon(iconNoteBmp, x, y, cell, songCol[ci][0], songCol[ci][1], songCol[ci][2]);
            break;
        }
        case CKI_KEYITEM: DrawBitmapIcon(iconKeyBmp,   x, y, cell, 180, 150, 90); break;
        default: break;
    }
}

// Small-font clip-with-ellipsis (CTextClip's sibling for CText6). Reusable beyond the checklist.
static void CText6Clip(int x, int y, const char *s, int maxw, u8 r, u8 g, u8 b)
{
    char buf[64]; int n = 0;
    while (s[n] && n < 62) { buf[n] = s[n]; n++; }
    buf[n] = 0;
    if (C6Width(buf) <= maxw) { CText6(x, y, buf, r, g, b); return; }
    while (n > 1)
    {
        buf[--n] = 0;
        char tmp[66]; int t = 0;
        for (int i = 0; i < n; ++i) tmp[t++] = buf[i];
        tmp[t++] = '.'; tmp[t++] = '.'; tmp[t] = 0;
        if (C6Width(tmp) <= maxw) { CText6(x, y, tmp, r, g, b); return; }
    }
    CText6(x, y, "..", r, g, b);
}
// Clipped small-font draw used by the marquee to paint only inside [clipX0, clipX1).
static void CText6ClipRegion(int x, int y, const char *s, int clipX0, int clipX1, u8 r, u8 g, u8 b)
{
    while (*s)
    {
        unsigned char ch = SmallAscii(SysFontUtf8Next(&s));
        if (ch)
        {
            const unsigned char *glyph = &font[ch * FONT_HEIGHT];
            for (int dy = 0; dy < FONT_HEIGHT; ++dy)
                for (int dx = 0; dx < FONT_WIDTH; ++dx)
                    if (glyph[dy] & (0x80 >> dx))
                    {
                        int X = x + dx, Y = y + dy;
                        if (X < clipX0 || X >= clipX1) continue;
                        if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
                        { u8 *p = CPix(X, Y); p[0] = r; p[1] = g; p[2] = b; }
                    }
        }
        x += FONT_WIDTH + 1;
    }
}
// Selected-row marquee: static (clipped) if it fits or the hold delay hasn't elapsed yet, else
// scrolls left and loops with a gap. `delay` is in frames (~16ms each) and is also the moment
// the scroll animation itself starts counting from, so it always starts smoothly at offset 0.
#define CHK_MARQUEE_DELAY      62    // ~1s - list rows
#define CHK_HINT_MARQUEE_DELAY 124   // ~2s - the item-card Hint field
#define CHK_SPEED_LIST 3, 2 // 1.5x  - list rows (task name)
#define CHK_SPEED_FAST 9, 4 // 2.25x - Hint/Where (1.5x on top of the 1.5x list speed)
static void CText6Marquee(int x, int y, int w, const char *s, int tick, int delay, int spdNum, int spdDen, u8 r, u8 g, u8 b)
{
    int tw = C6Width(s);
    if (tw <= w || tick < delay) { CText6Clip(x, y, s, w, r, g, b); return; }
    int cyclepx = tw + 24;
    int off = ((tick - delay) * spdNum / spdDen) % cyclepx;
    CText6ClipRegion(x - off, y, s, x, x + w, r, g, b);
    CText6ClipRegion(x - off + cyclepx, y, s, x, x + w, r, g, b);
}
// Wraps a short label into up to 2 lines within width w, breaking on spaces. A lone "&" is
// glued to the word that follows it BEFORE wrapping, so a line never ends on a dangling "&" -
// used for hub category names ("Zora's Domain & Jabu-Jabu" etc).
//
// Split choice is BALANCED, not greedy: if the whole label fits on one line, it stays on one
// line. Otherwise, among every point where it could break into 2 lines, pick the one that
// minimizes the longer of the two resulting line widths. A naive "pack line1 as full as
// possible" wrap strands short connector words ("an", "&") alone on line1 just because there
// happened to be room, e.g. "Becoming an" / "Adult" instead of the more even "Becoming" /
// "an Adult"; balancing avoids that.
typedef int (*ChkMeasureFn)(const char *);
static void ChkWrapBalanced(const char *s, int w, char *line1, char *line2, ChkMeasureFn measure)
{
    char buf[64]; int n = 0;
    while (s[n] && n < 62) { buf[n] = s[n]; n++; }
    buf[n] = 0;

    char *tok[12]; int ntok = 0;
    char *p = buf;
    while (*p && ntok < 12)
    {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        tok[ntok++] = p;
        while (*p && *p != ' ') p++;
    }
    char *mtok[12]; int nmtok = 0;
    for (int i = 0; i < ntok; ++i)
    {
        if (strcmp(tok[i], "&") == 0 && i + 1 < ntok)
        { *(tok[i + 1] - 1) = ' '; mtok[nmtok++] = tok[i]; ++i; } // "&" + next word -> one token
        else mtok[nmtok++] = tok[i];
    }

    line1[0] = 0; line2[0] = 0;
    if (nmtok == 0) return;

    char joined[64]; joined[0] = 0;
    for (int i = 0; i < nmtok; ++i)
    { char c[64]; if (joined[0]) sniprintf(c, sizeof c, "%s %s", joined, mtok[i]); else sniprintf(c, sizeof c, "%s", mtok[i]); strcpy(joined, c); }
    if (measure(joined) <= w) { strcpy(line1, joined); return; } // fits on one line - don't split it

    int bestSplit = -1, bestMax = 0x7FFFFFFF;
    char cur[64]; cur[0] = 0;
    for (int k = 0; k < nmtok; ++k)
    {
        char cand[64];
        if (cur[0]) sniprintf(cand, sizeof cand, "%s %s", cur, mtok[k]); else sniprintf(cand, sizeof cand, "%s", mtok[k]);
        strcpy(cur, cand);
        int w1 = measure(cur);
        if (w1 > w) break; // line1 can't extend this far and still fit
        int w2 = 0;
        if (k + 1 < nmtok)
        {
            char rest[64]; rest[0] = 0;
            for (int j = k + 1; j < nmtok; ++j)
            { char c2[64]; if (rest[0]) sniprintf(c2, sizeof c2, "%s %s", rest, mtok[j]); else sniprintf(c2, sizeof c2, "%s", mtok[j]); strcpy(rest, c2); }
            w2 = measure(rest);
        }
        int m = w1 > w2 ? w1 : w2;
        if (m < bestMax) { bestMax = m; bestSplit = k + 1; }
    }
    if (bestSplit < 0) bestSplit = 1; // even the first token alone overflows - force it anyway

    for (int i = 0; i < bestSplit; ++i)
    { char c[64]; if (line1[0]) sniprintf(c, sizeof c, "%s %s", line1, mtok[i]); else sniprintf(c, sizeof c, "%s", mtok[i]); strcpy(line1, c); }
    for (int i = bestSplit; i < nmtok; ++i)
    { char c[64]; if (line2[0]) sniprintf(c, sizeof c, "%s %s", line2, mtok[i]); else sniprintf(c, sizeof c, "%s", mtok[i]); strcpy(line2, c); }
}
#define CHK_BIGLINE_H 13 // stacked-line pitch for 2-line system-font labels (hub grid/buttons)
// Left-aligned 2-line wrap, system font, vertically centered within box height h (paired with a
// right-aligned count next to it, e.g. hub top screen). h-centering matters: a selection
// highlight covers the full row - if this always assumed 1 line, a 2-line label's 2nd line
// would spill out past the highlight instead of sitting inside it.
static void CTextWrap2(int x, int y, int w, int h, const char *s, u8 r, u8 g, u8 b)
{
    char line1[64], line2[64];
    ChkWrapBalanced(s, w, line1, line2, CTextWidth);
    int blockH = line2[0] ? (CHK_BIGLINE_H * 2) : CHK_BIGLINE_H;
    int ly = y + (h - blockH) / 2;
    CText(x, ly, line1, r, g, b, 0);
    if (line2[0]) CTextClip(x, ly + CHK_BIGLINE_H, line2, w, r, g, b, 0);
}
// 2-line wrap, system font, centered on BOTH axes inside a box (x,y,w,h) - e.g. touch buttons,
// so a 1-line name sits in the middle of the box instead of stuck to the top like a 2-line one.
static void CTextWrap2CenterBox(int x, int y, int w, int h, const char *s, u8 r, u8 g, u8 b)
{
    char line1[64], line2[64];
    ChkWrapBalanced(s, w, line1, line2, CTextWidth);
    int blockH = line2[0] ? (CHK_BIGLINE_H * 2) : CHK_BIGLINE_H;
    int ly = y + (h - blockH) / 2;
    CText(x + (w - CTextWidth(line1)) / 2, ly, line1, r, g, b, 0);
    if (line2[0])
    {
        int lw2 = CTextWidth(line2), ly2 = ly + CHK_BIGLINE_H;
        if (lw2 <= w) CText(x + (w - lw2) / 2, ly2, line2, r, g, b, 0);
        else          CTextClip(x, ly2, line2, w, r, g, b, 0);
    }
}

static int ChkFindKey(const char *key, int *outC, int *outI)
{
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
            if (strcmp(CHK_CATS[c].items[i].key, key) == 0) { *outC = c; *outI = i; return 1; }
    return 0;
}
static void ChecklistSave(void)
{
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, CHK_PATH),
                                 FS_OPEN_WRITE | FS_OPEN_CREATE, 0)))
        return;
    char buf[80]; u32 off = 0, wrote;
    static const char *hdr = "# OcarinaCTRComposer 100% Checklist. Plugin folder; survives updates.\nVER 1\n";
    FSFILE_Write(f, &wrote, off, hdr, (u32)strlen(hdr), FS_WRITE_FLUSH); off += wrote;
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
        {
            u8 s = chkState[c][i]; if (!s) continue;
            const char *tag = (s == 1) ? "A" : (s == 2) ? "M" : "S";
            int n = sniprintf(buf, sizeof buf, "STATE %s %s\n", CHK_CATS[c].items[i].key, tag);
            FSFILE_Write(f, &wrote, off, buf, (u32)n, FS_WRITE_FLUSH); off += wrote;
        }
    FSFILE_SetSize(f, off);
    FSFILE_Close(f);
}
// Wipe every item back to "not done" and rewrite Checklist.txt (bound to START on the hub).
static void ChecklistReset(void)
{
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
            chkState[c][i] = 0;
    ChecklistSave();
}
static void ChecklistLoad(void)
{
    chkLoaded = 1;
    memset(chkState, 0, sizeof(chkState));
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, CHK_PATH), FS_OPEN_READ, 0)))
        return;
    u64 sz64 = 0; FSFILE_GetSize(f, &sz64);
    u32 sz = (u32)sz64;
    if (sz == 0 || sz > 64 * 1024) { FSFILE_Close(f); return; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { FSFILE_Close(f); return; }
    u32 got = 0;
    Result r = FSFILE_Read(f, &got, 0, buf, sz);
    FSFILE_Close(f);
    if (R_FAILED(r) || !got) { free(buf); return; }
    buf[got] = 0;
    char *p = buf;
    while (*p)
    {
        char *line = p;
        while (*p && *p != '\n') p++;
        char *eol = p; if (*p) p++;
        if (eol > line && eol[-1] == '\r') eol[-1] = 0;
        *eol = 0;
        if (!strncmp(line, "STATE ", 6))
        {
            char *key = line + 6;
            char *sp = strchr(key, ' ');
            if (sp)
            {
                *sp = 0; char *tag = sp + 1;
                int c, i;
                if (ChkFindKey(key, &c, &i))
                {
                    u8 v = (tag[0] == 'A') ? 1 : (tag[0] == 'M') ? 2 : (tag[0] == 'S') ? 3 : 0;
                    if (v) chkState[c][i] = v;
                }
            }
        }
    }
    free(buf);
}
static void ChecklistLoadOnce(void) { if (!chkLoaded) ChecklistLoad(); }

// Auto-fill = SYNC the auto-marks to the CURRENTLY loaded save (so switching to a lesser save no
// longer keeps a previous save's marks). For each item it reads live gSaveContext:
//   detected  -> set state 1 (auto), but only from 0/3 - never overrides 2 (you manually checked)
//   NOT detected -> clear state 1 (stale auto) back to 0, but leave 2/3 (your own tracking) alone
// Detections are false-positive-proof, so re-marking a 3 (you-cleared) that the save confirms is safe.
// g_afAdd / g_afRem hold the last run's added / removed counts for the on-screen result.
static int g_afAdd, g_afRem;
static int ChecklistAutoFill(void)
{
    g_afAdd = g_afRem = 0;
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
        {
            const ChkItem *it = &CHK_CATS[c].items[i];
            int got = 0;
            if (it->kind == CK_EQUIP)          got = (R8(it->addr) & it->mask) != 0;
            else if (it->kind == CK_QUESTALL)  got = R8(it->addr) == it->mask;
            else if (it->kind == CK_INVSLOT)   got = it->mask ? (R8(it->addr) == it->mask)
                                                              : (R8(it->addr) != 0xFF);
            else if (it->kind == CK_UPGRADE)   got = ((R32(it->addr) >> (it->mask >> 3)) & 0x7)
                                                     >= (it->mask & 0x7);
            else continue; // not an auto-detectable item -> never auto-touch it
            u8 st = chkState[c][i];
            if (got) { if (st == 0 || st == 3) { chkState[c][i] = 1; ++g_afAdd; } }
            else     { if (st == 1)            { chkState[c][i] = 0; ++g_afRem; } }
        }
    return g_afAdd + g_afRem;
}
// Build the "+A -R" / "Up to date" result string for the auto-fill button.
static void ChkAfMsg(char *buf, const char **msg)
{
    if (g_afAdd || g_afRem) { siprintf(buf, "+%d -%d", g_afAdd, g_afRem); *msg = buf; }
    else *msg = "Up to date";
}

// Bottom screen is 320px wide, NOT the top window's 360 - every x below is native to that.
#define CHKB_L    8
#define CHKB_R    312
#define CHKB_COLW 148
// Hub grid is paginated 2 cols x 4 rows (8 categories/page) - a flat unpaginated grid stopped
// fitting once the dataset grew past a couple of test areas, and 2-line names need more row
// height than the old 1-line design had room for.
#define HUB_COLS   2
#define HUB_ROWS   4
#define HUB_PAGESZ (HUB_COLS * HUB_ROWS)
#define CHKB_BTN_H  36
#define CHKB_STRIDE 40
#define CHKB_TOP    10
// Marquee text-box widths, mirrored from the draw code below (task row / hint & where pill) -
// duplicated here so the pre-draw "is anything actually scrolling" check can be computed
// without doing a full draw pass.
#define CHK_TASK_TXTW ((CHKB_R - 16) - 6 - (CHKB_L + 16 + 8))
#define CHK_HINT_SVW  (WIN_X + WIN_W - 12 - (WIN_X + 86))
static void ToolChecklist(void)
{
    static int level = 0;             // 0 hub, 1 item list (persists across SELECT/reopen)
    static int catCur = 0;
    static int catCursor[CHK_NCATS], catScroll[CHK_NCATS];
    static int filterMode = 0;        // 0 All, 1 Todo, 2 Done
    static int revealLoc = 0;         // X toggle, sticky for the session
    static int selTick = 0;           // frames on the current item -> drives its marquee delay
    static const char *afMsg = NULL;  // Auto-fill result shown on the button (right side), timed
    static int afTick = 0;            // countdown for afMsg
    static char afBuf[24];
    ChecklistLoadOnce();
    if (cheatState[CH_CFG_AUTOFILL]) // auto-fill from the save every time the Checklist opens (Settings toggle)
    {
        ChecklistAutoFill(); ChecklistSave(); ChkAfMsg(afBuf, &afMsg);
        afTick = 180;
    }
    KbInit(); // touch service - every other touch-using tool calls this too

    int touchPrev = HidTouch(0, 0);
    u32 prev = HID_PAD;
    int redraw = 1;
    int lastCur = -1000;
    int hubPages = (CHK_NCATS + HUB_PAGESZ - 1) / HUB_PAGESZ;

    while (1)
    {
        int px, py, nowT = HidTouch(&px, &py);
        int tap = nowT && !touchPrev; touchPrev = nowT;
        if (!hidReady) tap = 0;
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        int selOverflow = 0; // is a marquee on the CURRENT item actually mid-scroll right now?

        if (down & BUTTON_SELECT) { g_quitToGame = 1; return; } // resumes at this level/cat/cursor

        if (level == 0)
        {
            if (down & BUTTON_B) return;
            int page = catCur / HUB_PAGESZ, local = catCur % HUB_PAGESZ;
            int pageBase = page * HUB_PAGESZ;
            int pageN = CHK_NCATS - pageBase; if (pageN > HUB_PAGESZ) pageN = HUB_PAGESZ;
            int pageRows = (pageN + HUB_COLS - 1) / HUB_COLS;
            int col = local % HUB_COLS, row = local / HUB_COLS;
            if (down & BUTTON_DOWN)
            { row = (row + 1) % pageRows; int ni = col + row * HUB_COLS;
              catCur = pageBase + ((ni < pageN) ? ni : (row * HUB_COLS < pageN ? row * HUB_COLS : pageN - 1)); redraw = 1; }
            if (down & BUTTON_UP)
            { row = (row - 1 + pageRows) % pageRows; int ni = col + row * HUB_COLS;
              catCur = pageBase + ((ni < pageN) ? ni : (row * HUB_COLS < pageN ? row * HUB_COLS : pageN - 1)); redraw = 1; }
            if (down & (BUTTON_LEFT | BUTTON_RIGHT))
            { int nc = (1 - col) + row * HUB_COLS; if (nc < pageN) catCur = pageBase + nc; redraw = 1; }
            if ((down & BUTTON_L1) && hubPages > 1)
            { int np = (page - 1 + hubPages) % hubPages, npBase = np * HUB_PAGESZ, npN = CHK_NCATS - npBase; if (npN > HUB_PAGESZ) npN = HUB_PAGESZ;
              catCur = npBase + (local < npN ? local : npN - 1); redraw = 1; }
            if ((down & BUTTON_R1) && hubPages > 1)
            { int np = (page + 1) % hubPages, npBase = np * HUB_PAGESZ, npN = CHK_NCATS - npBase; if (npN > HUB_PAGESZ) npN = HUB_PAGESZ;
              catCur = npBase + (local < npN ? local : npN - 1); redraw = 1; }
            if (tap)
            {
                for (int li = 0; li < pageN; ++li)
                {
                    int c2 = li % HUB_COLS, r2 = li / HUB_COLS, bx = CHKB_L + c2 * (CHKB_COLW + 8), by = CHKB_TOP + r2 * CHKB_STRIDE;
                    if (px >= bx && px < bx + CHKB_COLW && py >= by && py < by + CHKB_BTN_H)
                    { catCur = pageBase + li; level = 1; catCursor[pageBase + li] = 0; catScroll[pageBase + li] = 0; lastCur = -1000; redraw = 1; }
                }
                int gridBottom = CHKB_TOP + HUB_ROWS * CHKB_STRIDE - (CHKB_STRIDE - CHKB_BTN_H);
                int fy = gridBottom + 20;
                if (px >= CHKB_L && px < CHKB_R && py >= fy && py < fy + 24)
                { ChecklistAutoFill(); ChecklistSave(); ChkAfMsg(afBuf, &afMsg);
                  afTick = 180; redraw = 1; }
            }
            if (down & BUTTON_Y) { ChecklistAutoFill(); ChecklistSave(); ChkAfMsg(afBuf, &afMsg);
                afTick = 180; redraw = 1; }
            if (down & BUTTON_START) { ChecklistReset(); afMsg = "Reset"; afTick = 180; lastCur = -1000; redraw = 1; }
            if (down & BUTTON_A) { level = 1; catCursor[catCur] = 0; catScroll[catCur] = 0; lastCur = -1000; redraw = 1; }
            if (afTick > 0 && --afTick == 0) { afMsg = NULL; redraw = 1; } // clear the button result on timeout
        }
        else
        {
            const ChkCat *cc = &CHK_CATS[catCur];
            int cursor = catCursor[catCur], scroll = catScroll[catCur];

            // filtered index list for this category
            int filtIdx[CHK_MAXITEMS], filtN = 0;
            for (int i = 0; i < cc->count; ++i)
            {
                int checked = (chkState[catCur][i] == 1 || chkState[catCur][i] == 2);
                if (filterMode == 1 && checked) continue;
                if (filterMode == 2 && !checked) continue;
                filtIdx[filtN++] = i;
            }
            if (cursor >= filtN) cursor = filtN > 0 ? filtN - 1 : 0;

            if (down & BUTTON_B) { level = 0; redraw = 1; }
            if (down & BUTTON_L1) { catCur = (catCur + CHK_NCATS - 1) % CHK_NCATS; lastCur = -1000; redraw = 1; }
            if (down & BUTTON_R1) { catCur = (catCur + 1) % CHK_NCATS; lastCur = -1000; redraw = 1; }
            if (down & BUTTON_Y)  { filterMode = (filterMode + 1) % 3; redraw = 1; }
            if (down & BUTTON_X)  { revealLoc = !revealLoc; redraw = 1; }
            if (down & BUTTON_DOWN && filtN > 0) { cursor = (cursor + 1) % filtN; redraw = 1; }
            if (down & BUTTON_UP   && filtN > 0) { cursor = (cursor - 1 + filtN) % filtN; redraw = 1; }
            // D-Pad Left/Right page through the item list (8 rows visible); shoulders stay area-switch
            if (down & BUTTON_RIGHT && filtN > 0) { cursor += 8; if (cursor >= filtN) cursor = filtN - 1; redraw = 1; }
            if (down & BUTTON_LEFT  && filtN > 0) { cursor -= 8; if (cursor < 0) cursor = 0; redraw = 1; }

            if (tap && filtN > 0)
            {
                for (int r = 0; r < 8 && scroll + r < filtN; ++r)
                    if (px >= CHKB_L && px < CHKB_R && py >= 34 + r * 18 && py < 34 + r * 18 + 18)
                    { cursor = scroll + r; redraw = 1; }
            }
            if ((down & BUTTON_A) && filtN > 0)
            {
                int gi = filtIdx[cursor];
                int checked = (chkState[catCur][gi] == 1 || chkState[catCur][gi] == 2);
                chkState[catCur][gi] = (u8)(checked ? 3 : 2);
                ChecklistSave(); redraw = 1;
            }

            catCursor[catCur] = cursor; catScroll[catCur] = scroll;
            if (cursor != lastCur) { selTick = 0; lastCur = cursor; }
            else if (selTick < 100000) selTick++;

            // Mirrors CText6Marquee's own "tw > w && tick >= delay" trigger for each of the 3
            // marqueed fields on the current item, WITHOUT drawing anything - lets the redraw
            // gate below stay cheap (skip drawing) until a marquee is genuinely about to scroll,
            // instead of flipping to "redraw every tick" as soon as any delay elapses regardless
            // of whether that field even needs to scroll. Getting this wrong is exactly what
            // made the hint's 2s delay measure as ~10s before: once selTick passed the list-row
            // delay (62 ticks, ~1s), every tick started paying the ~100ms+ full-redraw cost even
            // though the hint field's own 124-tick delay hadn't elapsed yet - so the hint delay
            // was actually being counted in ~100ms+ ticks instead of ~16ms ones.
            if (filtN > 0)
            {
                const ChkItem *sit = &cc->items[filtIdx[cursor]];
                if (selTick >= CHK_MARQUEE_DELAY      && C6Width(sit->task) > CHK_TASK_TXTW) selOverflow = 1;
                if (selTick >= CHK_HINT_MARQUEE_DELAY && C6Width(sit->hint) > CHK_HINT_SVW)   selOverflow = 1;
                if (revealLoc && sit->loc[0] && selTick >= CHK_MARQUEE_DELAY && C6Width(sit->loc) > CHK_HINT_SVW - 6) selOverflow = 1;
            }
        }

        // A full redraw here means re-compositing the whole backdrop + bottom-screen frame -
        // expensive on this hardware (~100ms+). Only pay that cost when something actually
        // changed (redraw==1) or a marquee is genuinely mid-scroll (tick past its delay). While
        // just WAITING for a delay to elapse, nothing on screen changes, so we skip the redraw
        // entirely and let the ~16ms sleep above be the only per-tick cost - that's what keeps
        // the 1s/2s delays accurate; redrawing every tick during the wait was inflating each
        // "tick" to ~127ms, which is exactly why the delay measured ~8x too long.
        int animating = (level == 1) && selOverflow; // hub has no marquee anymore
        if (!redraw && !animating) continue;
        redraw = 0;

        // =========================================================== DRAW
        if (level == 0)
        {
            int page = catCur / HUB_PAGESZ, pageBase = page * HUB_PAGESZ;
            int pageN = CHK_NCATS - pageBase; if (pageN > HUB_PAGESZ) pageN = HUB_PAGESZ;

            ComposeBackdrop();
            CText(WIN_X + 12, WIN_Y + 7, T("Checklist 100%"), INK, 1);
            CFill(WIN_X + 12, WIN_Y + 24, CTextWidth(T("Checklist 100%")) + 6, 1, GOLD);
            if (hubPages > 1)
            {
                char pg[16]; siprintf(pg, "%d/%d", page + 1, hubPages);
                CText6(WIN_X + WIN_W - 12 - C6Width(pg), WIN_Y + 9, pg, INK_DIM);
            }
            int totalDone = 0, totalAll = 0;
            for (int c = 0; c < CHK_NCATS; ++c) // totals always cover ALL categories, not just this page
            {
                int done = 0;
                for (int i = 0; i < CHK_CATS[c].count; ++i)
                    if (chkState[c][i] == 1 || chkState[c][i] == 2) ++done;
                totalDone += done; totalAll += CHK_CATS[c].count;
            }
            int colW = ROW_W / 2, gridY = WIN_Y + 30, rowH = 28, rowGap = 3;
            for (int li = 0; li < pageN; ++li)
            {
                int c = pageBase + li;
                int done = 0;
                for (int i = 0; i < CHK_CATS[c].count; ++i)
                    if (chkState[c][i] == 1 || chkState[c][i] == 2) ++done;
                int col = li % HUB_COLS, row = li / HUB_COLS;
                int x = ROW_X + col * colW, y = gridY + row * (rowH + rowGap);
                int full = (CHK_CATS[c].count > 0 && done == CHK_CATS[c].count);
                int selc = (c == catCur);
                const u8 *nc = full ? CGREEN : (selc ? CGOLD : CINK);
                if (selc) CFillBlend(x - 3, y, colW - 6, rowH, 0, 0, 0, 110);
                char frac[16]; siprintf(frac, "%d/%d", done, CHK_CATS[c].count);
                int fw = C6Width(frac);
                int nameW = colW - fw - 16;
                CTextWrap2(x, y, nameW, rowH, CHK_CATS[c].name, nc[0], nc[1], nc[2]);
                CText6(x + colW - 12 - fw, y + (rowH - FONT_HEIGHT) / 2, frac, nc[0], nc[1], nc[2]);
            }
            int ty = gridY + HUB_ROWS * (rowH + rowGap) + 4;
            CFill(WIN_X + 12, ty, WIN_W - 24, 1, GOLD);
            CText6(WIN_X + 12, ty + 8, "Total", INK);
            char totFrac[24]; int pct = totalAll > 0 ? totalDone * 100 / totalAll : 0;
            siprintf(totFrac, "%d/%d (%d%%)", totalDone, totalAll, pct);
            CText6(WIN_X + WIN_W - 12 - C6Width(totFrac), ty + 8, totFrac, GREEN_ON);
            CFill(WIN_X + 12, ty + 20, WIN_W - 24, 5, 26, 20, 12);
            int barw = totalAll > 0 ? (WIN_W - 24) * totalDone / totalAll : 0;
            CFill(WIN_X + 12, ty + 20, barw, 5, GREEN_ON);
            int lx = WIN_X + 12, ly = ty + 31;
            const char *chips[4] = { "auto", "you", "todo", "cleared" };
            const u8 *chipc[4] = { CGREEN, CGOLD, CDIM, CDIM };
            for (int i = 0; i < 4; ++i)
            {
                CFill(lx, ly, 8, 8, chipc[i][0], chipc[i][1], chipc[i][2]);
                CText6(lx + 12, ly, chips[i], INK_DIM);
                lx += 12 + C6Width(chips[i]) + 6;
            }
            Present(); Present();

            for (int y = 0; y < BOT_H; ++y)
                for (int x = 0; x < BOT_W; ++x)
                {
                    u8 *p = CPix(x, y);
                    if (savedBotValid)
                    { u16 v = savedBot[y * BOT_W + x];
                      p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
                    else { p[0] = p[1] = p[2] = 12; }
                }
            CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
            CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
            for (int li = 0; li < pageN; ++li)
            {
                int i = pageBase + li;
                int col = li % HUB_COLS, row = li / HUB_COLS, bx = CHKB_L + col * (CHKB_COLW + 8), by = CHKB_TOP + row * CHKB_STRIDE;
                int done = 0;
                for (int k = 0; k < CHK_CATS[i].count; ++k)
                    if (chkState[i][k] == 1 || chkState[i][k] == 2) ++done;
                int full = (done == CHK_CATS[i].count);
                int sel = (i == catCur);
                const u8 *bc = full ? CGREEN : CGOLD;
                CFill(bx, by, CHKB_COLW, CHKB_BTN_H, sel ? 52 : 32, sel ? 44 : 25, sel ? 26 : 16);
                CFill(bx, by, CHKB_COLW, 1, bc[0], bc[1], bc[2]); CFill(bx, by + CHKB_BTN_H - 1, CHKB_COLW, 1, bc[0], bc[1], bc[2]);
                CFill(bx, by, 1, CHKB_BTN_H, bc[0], bc[1], bc[2]); CFill(bx + CHKB_COLW - 1, by, 1, CHKB_BTN_H, bc[0], bc[1], bc[2]);
                if (sel) CFill(bx, by, 4, CHKB_BTN_H, 255, 255, 255); // bright left bar - unmistakably "selected"
                // No fraction here - the top screen already shows X/Y per category, repeating it
                // on the touch buttons just stole width from the name and forced truncation.
                CTextWrap2CenterBox(bx + 6, by, CHKB_COLW - 12, CHKB_BTN_H, CHK_CATS[i].name, bc[0], bc[1], bc[2]);
            }
            int gridBottom = CHKB_TOP + HUB_ROWS * CHKB_STRIDE - (CHKB_STRIDE - CHKB_BTN_H);
            // above the Auto-fill box: navigation hints, horizontally centered
            const char *hUp = hubPages > 1 ? T("{DP} move   {A} open   {L}/{R} areas") : T("{DP} move   {A} open");
            CText6Btn((BOT_W - C6BtnWidth(hUp)) / 2, gridBottom + 6, hUp, INK_DIM);
            int fy = gridBottom + 20;
            CFill(CHKB_L, fy, CHKB_R - CHKB_L, 24, 20, 40, 20);
            CFill(CHKB_L, fy, CHKB_R - CHKB_L, 1, GREEN_ON); CFill(CHKB_L, fy + 23, CHKB_R - CHKB_L, 1, GREEN_ON);
            CFill(CHKB_L, fy, 1, 24, GREEN_ON); CFill(CHKB_R - 1, fy, 1, 24, GREEN_ON);
            int atw = C6Width(T("Auto-fill from save"));
            CText6(CHKB_L + (CHKB_R - CHKB_L - atw) / 2, fy + 7, T("Auto-fill from save"), GREEN_ON);
            if (afMsg) // on-screen result, right side of the button (no need to leave the screen)
            {
                const char *m = T(afMsg);
                if (afMsg[0] == 'N' || afMsg[0] == 'R') CText6(CHKB_R - 8 - C6Width(m), fy + 7, m, INK_DIM); // Nothing new / Reset
                else                                    CText6(CHKB_R - 8 - C6Width(m), fy + 7, m, 255, 236, 120); // OK: +N
            }
            // below the box: action hints, horizontally centered (START has no glyph, shown as text)
            const char *hDn = T("{Y} Auto-fill    START reset");
            CText6Btn((BOT_W - C6BtnWidth(hDn)) / 2, fy + 30, hDn, INK_DIM);
            BotBlitComposeBoth();
        }
        else
        {
            const ChkCat *cc = &CHK_CATS[catCur];
            int cursor = catCursor[catCur], scroll = catScroll[catCur];
            int filtIdx[CHK_MAXITEMS], filtN = 0;
            for (int i = 0; i < cc->count; ++i)
            {
                int checked = (chkState[catCur][i] == 1 || chkState[catCur][i] == 2);
                if (filterMode == 1 && checked) continue;
                if (filterMode == 2 && !checked) continue;
                filtIdx[filtN++] = i;
            }
            if (cursor >= filtN) cursor = filtN > 0 ? filtN - 1 : 0;
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + 8) scroll = cursor - 7;
            catCursor[catCur] = cursor; catScroll[catCur] = scroll;

            ComposeBackdrop();
            CTextClip(WIN_X + 12, WIN_Y + 7, cc->name, 200, INK, 1);
            int done = 0; for (int i = 0; i < cc->count; ++i) if (chkState[catCur][i]==1||chkState[catCur][i]==2) ++done;
            char hdr[16]; siprintf(hdr, "%d/%d", done, cc->count);
            CText6(WIN_X + WIN_W - 12 - C6Width(hdr), WIN_Y + 9, hdr, INK_DIM);
            CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);

            if (filtN > 0)
            {
                int gi = filtIdx[cursor];
                const ChkItem *it = &cc->items[gi];
                int bx = WIN_X + 12, by = WIN_Y + 34;
                CFill(bx, by, 66, 66, 26, 20, 12); CFill(bx, by, 66, 1, GOLD); CFill(bx, by+65, 66, 1, GOLD);
                CFill(bx, by, 1, 66, GOLD); CFill(bx+65, by, 1, 66, GOLD);
                DrawChkIcon(it, bx + 13, by + 13, 5); // 40px icon, centered in the 66px box
                int tx = bx + 76;
                CTextClip(tx, by, it->task, WIN_X + WIN_W - 12 - tx, GOLD, 0);
                u8 st = chkState[catCur][gi];
                int fy2 = by + 72;
                CFill(WIN_X + 12, fy2, WIN_W - 24, 1, 120, 98, 50); // hairline
                int ly2 = fy2 + 10;
                CText6(WIN_X + 12, ly2, T("Status"), GOLD);
                const char *statTxt = st == 1 ? T("From your save") : st == 2 ? T("Checked by you")
                                     : st == 3 ? T("You cleared this") : T("Not done yet");
                const u8 *statC = st == 1 ? CGREEN : st == 2 ? CGOLD : st == 3 ? CGOLD : CDIM;
                u8 sc[3]; LiftForDark(statC[0], statC[1], statC[2], sc); // keep legible on the dark pill (dark-accent themes)
                int svx = WIN_X + 86, svw = WIN_X + WIN_W - 12 - svx;
                CFill(svx, ly2 - 2, svw, 14, 20, 16, 10);
                CFill(svx, ly2 - 2, svw, 1, sc[0], sc[1], sc[2]); CFill(svx, ly2+11, svw, 1, sc[0], sc[1], sc[2]);
                CText6Clip(svx + (svw - C6Width(statTxt)) / 2, ly2, statTxt, svw - 6, sc[0], sc[1], sc[2]);

                int hy = ly2 + 24;
                CText6(WIN_X + 12, hy, T("Hint"), GOLD);
                CText6Marquee(svx, hy, svw, it->hint, selTick, CHK_HINT_MARQUEE_DELAY, CHK_SPEED_FAST, 236, 236, 210);

                int oy = hy + 20;
                CText6(WIN_X + 12, oy, T("Where"), GOLD);
                if (!it->loc[0]) CText6(svx, oy, "-", 140, 130, 104);
                else if (!revealLoc)
                {
                    CFill(svx, oy - 2, svw, 14, 20, 16, 10);
                    CFill(svx, oy - 2, svw, 1, GREEN_ON); CFill(svx, oy+11, svw, 1, GREEN_ON);
                    const char *lk = T("X: show location");
                    CText6Clip(svx + (svw - C6Width(lk)) / 2, oy, lk, svw - 6, GREEN_ON);
                }
                else
                {
                    CFill(svx, oy - 2, svw, 14, 20, 16, 10);
                    CFill(svx, oy - 2, svw, 1, GREEN_ON); CFill(svx, oy+11, svw, 1, GREEN_ON);
                    CText6Marquee(svx + 3, oy, svw - 6, it->loc, selTick, CHK_MARQUEE_DELAY, CHK_SPEED_FAST, GREEN_ON);
                }
            }
            else CText6Clip(WIN_X + 12, WIN_Y + 100, T("Nothing here - try Y to change the filter."), WIN_W - 24, INK_DIM);
            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{L}/{R} area   {X} location"), INK_DIM);
            Present(); Present();

            for (int y = 0; y < BOT_H; ++y)
                for (int x = 0; x < BOT_W; ++x)
                {
                    u8 *p = CPix(x, y);
                    if (savedBotValid)
                    { u16 v = savedBot[y * BOT_W + x];
                      p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
                    else { p[0] = p[1] = p[2] = 12; }
                }
            CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
            CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
            const char *fl = filterMode == 0 ? "All" : filterMode == 1 ? "Todo" : "Done";
            char flbuf[24]; sniprintf(flbuf, sizeof flbuf, "%s %d/%d", fl, done, cc->count);
            CText6(CHKB_L, 8, "<", INK_DIM);
            CTextClip(20, 6, cc->name, 190, GOLD, 0);
            CText6(CHKB_R - 8 - C6Width(flbuf) - 10, 8, flbuf, INK_DIM);
            CText6(CHKB_R - 8, 8, ">", INK_DIM);
            CFill(CHKB_L, 26, CHKB_R - CHKB_L, 1, GOLD);
            for (int r = 0; r < 8 && scroll + r < filtN; ++r)
            {
                int gi = filtIdx[scroll + r]; int y = 34 + r * 18;
                int isCur = (scroll + r == cursor);
                if (isCur) CFillBlend(CHKB_L, y - 1, CHKB_R - CHKB_L, 18, 0, 0, 0, 110);
                const ChkItem *it = &cc->items[gi];
                DrawChkIcon(it, CHKB_L, y + 1, 2);
                u8 st = chkState[catCur][gi];
                int tx = CHKB_L + 16 + 8; // icon is 16px wide - 8px gap so text doesn't look glued to it
                int sx = CHKB_R - 16, sy = y + 3, txtw = sx - 6 - tx;
                if (isCur) CText6Marquee(tx, y + 4, txtw, it->task, selTick, CHK_MARQUEE_DELAY, CHK_SPEED_LIST, 236, 200, 120);
                else       CText6Clip(tx, y + 4, it->task, txtw, 236, 236, 210);
                if (st == 1) CFill(sx, sy, 9, 9, GREEN_ON);
                else if (st == 2) CFill(sx, sy, 9, 9, GOLD);
                else { CFill(sx, sy, 9, 1, 140,130,104); CFill(sx, sy+8, 9, 1, 140,130,104);
                       CFill(sx, sy, 1, 9, st==3?230:140, st==3?200:130, st==3?90:104);
                       CFill(sx+8, sy, 1, 9, st==3?230:140, st==3?200:130, st==3?90:104); }
            }
            if (filtN == 0) CText6(140, 100, T("Nothing here."), INK_DIM);
            // scroll arrows (same gold triangles the main menu uses): more items above/below
            int sax = (CHKB_L + CHKB_R) / 2;
            if (scroll > 0)
                for (int i = 0; i < 4; ++i) { int w = 1 + 2 * i; CFill(sax - w / 2, 28 + i, w, 1, GOLD); }
            if (scroll + 8 < filtN)
                for (int i = 0; i < 4; ++i) { int w = 7 - 2 * i; CFill(sax - w / 2, 190 + i, w, 1, GOLD); }
            { const char *hf = T("{DP} move   {A} mark/clear   {Y} filter");
              CText6Btn((BOT_W - C6BtnWidth(hf)) / 2, 210, hf, INK_DIM); }
            BotBlitComposeBoth();
        }
    }
}
