// Reader: word-wrap a body into visual lines, then scroll through them.
#define GR_MAXLINES 2000
static u16 g_glOff[GR_MAXLINES];
static u16 g_glLen[GR_MAXLINES];
static int g_glN;
static void GuideWrap(const char *s, int cols)
{
    g_glN = 0;
    int len = 0; while (s[len]) len++;
    int i = 0;
    while (i < len && g_glN < GR_MAXLINES)
    {
        int start = i, lastSpace = -1, count = 0;
        while (i < len && count < cols && s[i] != '\n')
        {
            if (s[i] == ' ') lastSpace = i;
            i++; count++;
        }
        int end;
        if (i < len && s[i] == '\n')                  { end = i; i++; }               // hard break
        else if (i >= len)                            { end = i; }                     // end of text
        else if (count >= cols && lastSpace > start)  { end = lastSpace; i = lastSpace + 1; } // wrap at space
        else                                          { end = i; }                     // long word / hard cut
        g_glOff[g_glN] = (u16)start; g_glLen[g_glN] = (u16)(end - start); g_glN++;
    }
}

// static branded bottom panel while a guide is open
static void GuideBottom(const char *subtitle)
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
    CText(14, 10, T("Game Guide"), GOLD, 1);
    CFill(14, 32, C6Width(T("Game Guide")) * 2, 1, GOLD);
    CText6(14, 44, subtitle, INK);   // guide content: stays English
    CText6(14, 70, T("Read on the top screen."), INK_DIM);
    CText6Btn(14, 86, T("{DP} / {L}/{R} : scroll or move"), INK_DIM);
    CText6Btn(14, 100, T("{A} open     {B} back"), INK_DIM);
    CText6(14, 114, T("SELECT : back to the game"), INK_DIM);
    BotBlitComposeBoth();
}

// Solid dark-brown window (gold border) instead of the parchment + triforce,
// so dense guide text is easy to read.
static void GuideBackdrop(void)
{
    RestoreTopBackdrop();
    CFill(WIN_X, WIN_Y, WIN_W, WIN_H, BG); // solid theme background for easy reading
    CFill(WIN_X, WIN_Y, WIN_W, 2, GOLD);
    CFill(WIN_X, WIN_Y + WIN_H - 2, WIN_W, 2, GOLD);
    CFill(WIN_X, WIN_Y, 2, WIN_H, GOLD);
    CFill(WIN_X + WIN_W - 2, WIN_Y, 2, WIN_H, GOLD);
}

// Scrollable reader. Returns 1 on B (back); sets g_quitToGame and returns 0 on SELECT.
static int GuideReader(const char *title, const char *body, int *scrollIO)
{
    int cols = (WIN_W - 30) / 7;   // chars/line at 7px advance (~41)
    GuideWrap(body, cols);
    int rows = 12, redraw = 1;
    int scroll = scrollIO ? *scrollIO : 0;
    if (scroll > g_glN) scroll = 0;
    u32 prev = HID_PAD;
    while (1)
    {
        int maxScroll = (g_glN > rows) ? g_glN - rows : 0;
        if (redraw)
        {
            GuideBackdrop();
            CText(WIN_X + 12, WIN_Y + 6, title, GOLD, 1);
            char pi[16];
            siprintf(pi, "%d%%", maxScroll ? scroll * 100 / maxScroll : 100);
            CText6(WIN_X + WIN_W - 12 - C6Width(pi), WIN_Y + 9, pi, INK_DIM);
            CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);
            for (int r = 0; r < rows; ++r)
            {
                int li = scroll + r;
                if (li >= g_glN) break;
                char buf[64];
                int len = g_glLen[li]; if (len > 63) len = 63;
                memcpy(buf, body + g_glOff[li], (size_t)len); buf[len] = 0;
                CText6(WIN_X + 14, WIN_Y + 28 + r * 13, buf, INK);
            }
            if (g_glN > rows)
            {
                int trackH = rows * 13;
                int barH = trackH * rows / g_glN; if (barH < 8) barH = 8;
                int barY = WIN_Y + 28 + (trackH - barH) * scroll / maxScroll;
                CFill(WIN_X + WIN_W - 15, WIN_Y + 28, 3, trackH, 40, 32, 20);
                CFill(WIN_X + WIN_W - 15, barY, 3, barH, GOLD);
            }
            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{DP} / {L}/{R} scroll   {B} back"), INK_DIM);
            Present(); Present();
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if ((down & BUTTON_DOWN) && scroll < maxScroll) { scroll++; redraw = 1; }
        if ((down & BUTTON_UP)   && scroll > 0)         { scroll--; redraw = 1; }
        if (down & BUTTON_R1) { scroll += rows; if (scroll > maxScroll) scroll = maxScroll; redraw = 1; }
        if (down & BUTTON_L1) { scroll -= rows; if (scroll < 0) scroll = 0; redraw = 1; }
        if (down & BUTTON_B) { if (scrollIO) *scrollIO = scroll; return 1; }
        if (down & BUTTON_SELECT) { if (scrollIO) *scrollIO = scroll; g_quitToGame = 1; return 0; }
    }
}

// Titled list with a cursor. Same look as the main menu (system font, folder
// icons, ROW_H rows). Returns chosen index, or -1 on B, -2 on SELECT (quit).
static int GuideList(const char *title, const char **labels, int count, int initSel, int *outSel)
{
    int sel = (initSel >= 0 && initSel < count) ? initSel : 0, scroll = 0, redraw = 1;
    u32 prev = HID_PAD;
    while (1)
    {
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + MAX_ROWS) scroll = sel - MAX_ROWS + 1;
        if (outSel) *outSel = sel;
        if (redraw)
        {
            ComposeBackdrop();
            int tw = CTextWidth(title);
            CText(WIN_X + 12, WIN_Y + 7, title, INK, 1);
            CFill(WIN_X + 12, WIN_Y + 24, tw + 6, 1, GOLD);
            for (int r = 0; r < MAX_ROWS; ++r)
            {
                int i = scroll + r; if (i >= count) break;
                int y = ROW_Y0 + r * ROW_H;
                if (i == sel)
                {
                    CFillBlend(ROW_X - 4, y - 1, ROW_W + 8, ROW_H, 0, 0, 0, 110);
                    CFill(ROW_X - 4, y - 1, 2, ROW_H, GOLD);
                }
                FolderIconSmall(ROW_X, y + 1);
                CText(ROW_X + 20, y - 1, labels[i], INK, 0);
            }
            if (scroll > 0)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, ROW_Y0 + 3 + a, 1 + 2 * a, 1, GOLD);
            if (scroll + MAX_ROWS < count)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, ROW_Y0 + MAX_ROWS * ROW_H - 4 - a, 1 + 2 * a, 1, GOLD);
            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 16, T("{A} open   {B} back   SELECT: game"), INK_DIM);
            Present(); Present();
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if (down & BUTTON_DOWN) { sel = (sel + 1 < count) ? sel + 1 : 0; redraw = 1; }
        if (down & BUTTON_UP)   { sel = (sel > 0) ? sel - 1 : count - 1; redraw = 1; }
        if (down & BUTTON_A)      return sel;
        if (down & BUTTON_B)      return -1;
        if (down & BUTTON_SELECT) return -2;
    }
}
