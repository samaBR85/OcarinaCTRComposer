// ===================== CTRPF-style rendering (parchment window) =====================
#define WIN_X   40
#define WIN_Y   20
#define WIN_W   320
#define WIN_H   200
#define ROW_X   (WIN_X + 12)
#define ROW_W   (WIN_W - 24)
#define ROW_Y0  (WIN_Y + 32)
#define ROW_H   16
#define MAX_ROWS 9

// Live theme: the color macros expand to runtime arrays, so switching a theme
// just rewrites these and every CFill/CText call follows. Defaults = Zelda Classic.
static u8 CINK[3]   = { 248, 240, 216 };
static u8 CDIM[3]   = { 196, 180, 150 };
static u8 CGOLD[3]  = { 236, 200, 120 };
static u8 CGREEN[3] = { 140, 236, 120 };
static u8 CBG[3]    = { 70, 55, 34 };
#define INK      CINK[0],   CINK[1],   CINK[2]
#define INK_DIM  CDIM[0],   CDIM[1],   CDIM[2]
#define GOLD     CGOLD[0],  CGOLD[1],  CGOLD[2]
#define GREEN_ON CGREEN[0], CGREEN[1], CGREEN[2]
#define BG       CBG[0],    CBG[1],    CBG[2]

// Expand a theme color ARRAY into the r,g,b argument triple. Use this - never the macros above -
// whenever the color is chosen by a condition.
//
// WHY: `on ? GREEN_ON : INK` reads correctly and compiles silently, but it is wrong. `?:` binds
// tighter than `,`, and a comma expression is legal as the middle operand, so it expands to
//     (on ? (CGREEN[0], CGREEN[1], CGREEN[2]) : CINK[0]),  CINK[1],  CINK[2]
// i.e. red = CGREEN[2] when on, and green/blue ALWAYS come from INK. An "on" row rendered
// (120,240,216) cyan instead of (140,236,120) green. Select the array first, expand it once.
#define RGB3(c)  (c)[0], (c)[1], (c)[2]

// Some themes have a light window bg; hardcoded light text then vanishes. Pick text
// colors from the bg luminance so overlays (e.g. the quick menu) stay readable everywhere.
static int ThemeBgLight(void) { return (CBG[0] * 30 + CBG[1] * 59 + CBG[2] * 11) / 100 > 140; }

// A fixed dark inset field (status pills, dark tooltips) needs text that stays legible even when
// the active theme's accent is itself dark (e.g. Zelda BotW gold ~ {59,49,42}, Wild West brown).
// Lift a too-dark color toward white, preserving hue, so it reads on the ~{20,16,10} inset.
// Bright colors pass through unchanged, so it's safe to apply blindly.
static void LiftForDark(u8 r, u8 g, u8 b, u8 *o)
{
    int lum = (r * 30 + g * 59 + b * 11) / 100;
    if (lum >= 96) { o[0] = r; o[1] = g; o[2] = b; return; }
    int t = 96 - lum; if (t > 78) t = 78; // blend % toward white, capped so hue survives
    o[0] = (u8)(r + (255 - r) * t / 100);
    o[1] = (u8)(g + (255 - g) * t / 100);
    o[2] = (u8)(b + (255 - b) * t / 100);
}

static void ApplyTheme(int idx)
{
    if (idx < 0 || idx >= THEME_COUNT) idx = 0;
    const Theme *t = &THEMES[idx];
    for (int i = 0; i < 3; ++i)
    {
        CGOLD[i] = t->gold[i]; CINK[i] = t->ink[i]; CDIM[i] = t->dim[i];
        CGREEN[i] = t->green[i]; CBG[i] = t->bg[i];
    }
    g_themeIdx = idx; g_themeParchment = t->parchment;
}

static void DimOutsideWindow(void)
{
    for (int y = 0; y < TOP_H; ++y)
    {
        u8 *p = CPix(0, y);
        int inWinY = (y >= WIN_Y && y < WIN_Y + WIN_H);
        for (int x = 0; x < TOP_W; ++x, p += 3)
        {
            if (inWinY && x >= WIN_X && x < WIN_X + WIN_W) continue;
            p[0] = (u8)(p[0] * 130 / 255); p[1] = (u8)(p[1] * 130 / 255); p[2] = (u8)(p[2] * 130 / 255);
        }
    }
}

// Snapshot / restore the full top backdrop (dimmed game frame). Both screens
// share gCompose, so drawing the bottom can bleed into the top's out-of-window
// area; restoring the full backdrop before every top redraw keeps it clean.
static void CaptureTopBackdrop(void)
{
    if (!savedTop) return;
    for (int y = 0; y < TOP_H; ++y)
        for (int x = 0; x < TOP_W; ++x)
        {
            u8 *p = CPix(x, y);
            savedTop[y * TOP_W + x] = (u16)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
        }
    savedTopValid = 1;
}
static void RestoreTopBackdrop(void)
{
    if (!savedTopValid) return;
    for (int y = 0; y < TOP_H; ++y)
        for (int x = 0; x < TOP_W; ++x)
        {
            u16 v = savedTop[y * TOP_W + x];
            u8 *p = CPix(x, y);
            p[0] = (u8)(((v >> 11) & 31) << 3); p[1] = (u8)(((v >> 5) & 63) << 2); p[2] = (u8)((v & 31) << 3);
        }
}

static void ComposeBackdrop(void)
{
    RestoreTopBackdrop(); // full top from the saved backdrop (erases any bleed)
    if (!g_themeParchment) // solid-color themed window with a gold border
    {
        CFill(WIN_X, WIN_Y, WIN_W, WIN_H, BG);
        CFill(WIN_X, WIN_Y, WIN_W, 2, GOLD); CFill(WIN_X, WIN_Y + WIN_H - 2, WIN_W, 2, GOLD);
        CFill(WIN_X, WIN_Y, 2, WIN_H, GOLD); CFill(WIN_X + WIN_W - 2, WIN_Y, 2, WIN_H, GOLD);
        return;
    }
    for (int y = 0; y < WIN_H; ++y) // Zelda Classic: the parchment image
    {
        const u16 *src = &topbg[y * TOPBG_W];
        u8 *p = CPix(WIN_X, WIN_Y + y);
        for (int x = 0; x < TOPBG_W; ++x, p += 3)
        {
            u16 v = src[x];
            p[0] = (u8)(((v >> 11) & 31) << 3);
            p[1] = (u8)(((v >> 5) & 63) << 2);
            p[2] = (u8)((v & 31) << 3);
        }
    }
}

static void CheckBoxIcon(int x, int y, int on)
{
    CFillBlend(x, y, 12, 12, 0, 0, 0, 70);
    CFill(x, y, 12, 1, GOLD); CFill(x, y + 11, 12, 1, GOLD);
    CFill(x, y, 1, 12, GOLD); CFill(x + 11, y, 1, 12, GOLD);
    if (on)
    {
        for (int i = 0; i < 3; ++i) { CFill(x + 2 + i, y + 5 + i, 2, 2, GREEN_ON); }
        for (int i = 0; i < 5; ++i) { CFill(x + 4 + i, y + 8 - i, 2, 2, GREEN_ON); }
    }
}

static void FolderIconSmall(int x, int y)
{
    CFill(x, y + 1, 6, 3, 172, 128, 34);
    CFill(x, y + 3, 13, 9, 219, 172, 66);
    CFill(x + 1, y + 4, 11, 2, 240, 205, 120);
    CFill(x, y + 3, 13, 1, 130, 92, 20);
}

// 9x9 checkbox for the compact quick menu
static void CheckBoxIconS(int x, int y, int on)
{
    CFillBlend(x, y, 9, 9, 0, 0, 0, 70);
    CFill(x, y, 9, 1, GOLD); CFill(x, y + 8, 9, 1, GOLD);
    CFill(x, y, 1, 9, GOLD); CFill(x + 8, y, 1, 9, GOLD);
    if (on)
    {
        CFill(x + 2, y + 4, 2, 2, GREEN_ON);
        CFill(x + 3, y + 5, 2, 2, GREEN_ON);
        for (int i = 0; i < 4; ++i) CFill(x + 4 + i, y + 5 - i, 2, 1, GREEN_ON);
    }
}
// 9x9 placeholder for the quick menu's left column on rows that are NOT on/off toggles
// (actions/shortcuts). Same fill as the checkbox's interior (black @ ~27% over the panel) but with
// NO gold border - so it reads as a solid tinted square, not a toggle. Theme-aware like the checkbox.
static void BrownBoxS(int x, int y)
{
    CFillBlend(x, y, 9, 9, 0, 0, 0, 70);
}
// True only for genuine on/off toggles (the cheats ApplyCheats holds continuously), plus Toggle Age
// (a momentary toggle). Everything else is a one-shot action/shortcut -> brown box, not a checkbox.
// Keep in sync with the cheatState[] uses in ApplyCheats.
static int IsToggleCheat(int id)
{
    switch (id)
    {
        case CH_MOONJUMP: case CH_FASTMOVE: case CH_EPONA_CARROTS: case CH_EPONA_CARROTS_ALL:
        case CH_EPONA_MJ: case CH_INVINCIBLE: case CH_HEARTS_MAX: case CH_HEARTS_KEEP:
        case CH_SPIN: case CH_SWORD_GLITCH: case CH_STICK_FIRE: case CH_NAYRU:
        case CH_ARROWS: case CH_NUTS: case CH_STICKS: case CH_BOMBS: case CH_BOMBCHU:
        case CH_SLING: case CH_EXPLOSIVES: case CH_SKULLTULA: case CH_RUPEES: case CH_T_MOD:
        case CH_RAIN: case CH_FREEZE_TIME: case CH_Q_KEYS: case CH_CHEST_MANY:
        case CH_HEARTPIECE_MANY: case CH_KNIFE_NOBREAK: case CH_SKIP_SONG: case CH_TOGGLE_AGE:
            return 1;
        default: return 0;
    }
}
static int C6Width(const char *s)
{
    int n = 0;
    while (*s) { if (SmallAscii(SysFontUtf8Next(&s))) n++; }
    return n * (FONT_WIDTH + 1);
}
// Draw a 14px button glyph (A/B/X/Y/L/R/D-pad) with alpha at (x,y).
static void DrawGlyph(int x, int y, int id)
{
    if (id < 0 || id >= NUM_GLYPHS) return;
    const unsigned short *px = glyphs[id];
    for (int yy = 0; yy < GLY; ++yy)
        for (int xx = 0; xx < GLY; ++xx)
        {
            unsigned short v = px[yy * GLY + xx];
            u32 a = (u32)(v & 0xF) * 17;
            if (!a) continue;
            int X = x + xx, Y = y + yy;
            if ((unsigned)X >= TOP_W || (unsigned)Y >= TOP_H) continue;
            u8 r = (u8)(((v >> 12) & 0xF) * 17), g = (u8)(((v >> 8) & 0xF) * 17), b = (u8)(((v >> 4) & 0xF) * 17);
            u8 *p = CPix(X, Y);
            p[0] = (u8)((p[0] * (255 - a) + r * a) / 255);
            p[1] = (u8)((p[1] * (255 - a) + g * a) / 255);
            p[2] = (u8)((p[2] * (255 - a) + b * a) / 255);
        }
}
// Generic RGBA4444 image blit with alpha (used for the About-screen title logo).
static void DrawImg(int x, int y, const unsigned short *px, int w, int h)
{
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
        {
            unsigned short v = px[yy * w + xx];
            u32 a = (u32)(v & 0xF) * 17;
            if (!a) continue;
            int X = x + xx, Y = y + yy;
            if ((unsigned)X >= TOP_W || (unsigned)Y >= TOP_H) continue;
            u8 r = (u8)(((v >> 12) & 0xF) * 17), g = (u8)(((v >> 8) & 0xF) * 17), b = (u8)(((v >> 4) & 0xF) * 17);
            u8 *p = CPix(X, Y);
            p[0] = (u8)((p[0] * (255 - a) + r * a) / 255);
            p[1] = (u8)((p[1] * (255 - a) + g * a) / 255);
            p[2] = (u8)((p[2] * (255 - a) + b * a) / 255);
        }
}
// Small-font text with inline button glyphs. Tokens: {A}{B}{X}{Y}{L}{R}{DP} become glyph icons.
// The glyph is drawn a touch above the text baseline so it centers on the ~10px line.
static int GlyphTok(const char *s) // returns glyph id if s points at a token, else -1
{
    if (s[0] != '{') return -1;
    if (s[1] == 'D' && s[2] == 'P' && s[3] == '}') return GL_DP;
    if (s[2] != '}') return -1;
    switch (s[1]) { case 'A': return GL_A; case 'B': return GL_B; case 'X': return GL_X;
                    case 'Y': return GL_Y; case 'L': return GL_L; case 'R': return GL_R; }
    return -1;
}
static void CText6Btn(int x, int y, const char *s, u8 r, u8 g, u8 b)
{
    while (*s)
    {
        int id = GlyphTok(s);
        if (id >= 0) { DrawGlyph(x, y - 3, id); x += GLY + 1; s += (id == GL_DP) ? 4 : 3; continue; }
        char one[2] = { *s, 0 };
        CText6(x, y, one, r, g, b);
        x += FONT_WIDTH + 1;
        s++;
    }
}
// Width in px of a CText6Btn string (glyph tokens count as GLY+1).
static int C6BtnWidth(const char *s)
{
    int w = 0;
    while (*s)
    {
        int id = GlyphTok(s);
        if (id >= 0) { w += GLY + 1; s += (id == GL_DP) ? 4 : 3; }
        else { w += FONT_WIDTH + 1; s++; }
    }
    return w;
}

// Large (system-font) text with inline 14px button glyphs. Same {A}{B}{X}{Y}{L}{R}{DP}
// tokens as CText6Btn; used on the pause help screen so controls show real 3DS buttons.
static void CTextBtn(int x, int y, const char *s, u8 r, u8 g, u8 b, int bold)
{
    char run[128];
    while (*s)
    {
        int n = 0;
        while (*s && GlyphTok(s) < 0 && n < 127) run[n++] = *s++;
        if (n) { run[n] = 0; CText(x, y, run, r, g, b, bold); x += CTextWidth(run); }
        int id = GlyphTok(s);
        if (id >= 0) { DrawGlyph(x, y + 1, id); x += GLY + 2; s += (id == GL_DP) ? 4 : 3; }
    }
}
// Pixel width of a CTextBtn string (glyph tokens count as GLY+2, text runs measured natively).
static int CTextBtnWidth(const char *s)
{
    int w = 0; char run[128];
    while (*s)
    {
        int n = 0;
        while (*s && GlyphTok(s) < 0 && n < 127) run[n++] = *s++;
        if (n) { run[n] = 0; w += CTextWidth(run); }
        int id = GlyphTok(s);
        if (id >= 0) { w += GLY + 2; s += (id == GL_DP) ? 4 : 3; }
    }
    return w;
}
// CTextBtn with truncation to maxw px (appends ".."). Tokens stay whole; only used for cheat labels.
static void CTextClipBtn(int x, int y, const char *s, int maxw, u8 r, u8 g, u8 b, int bold)
{
    if (CTextBtnWidth(s) <= maxw) { CTextBtn(x, y, s, r, g, b, bold); return; }
    int dots = CTextWidth("..");
    int cx = x;
    while (*s)
    {
        int id = GlyphTok(s);
        char one[2] = { *s, 0 };
        int uw = (id >= 0) ? (GLY + 2) : CTextWidth(one);
        if (cx + uw > x + maxw - dots) break;
        if (id >= 0) { DrawGlyph(cx, y + 1, id); s += (id == GL_DP) ? 4 : 3; }
        else         { CText(cx, y, one, r, g, b, bold); s++; }
        cx += uw;
    }
    CText(cx, y, "..", r, g, b, bold);
}

// small gold "bottle" icon for pickers
static void BottleIcon(int x, int y)
{
    CFill(x + 4, y, 4, 2, 236, 200, 120);       // cork
    CFill(x + 3, y + 2, 6, 2, 160, 190, 220);   // neck
    CFill(x + 2, y + 4, 8, 8, 120, 170, 220);   // body
    CFill(x + 3, y + 5, 2, 5, 200, 230, 255);   // shine
}

static void StarIcon(int x, int y)
{
    static const u8 rows[7] = { 0x08, 0x1C, 0x7F, 0x3E, 0x1C, 0x36, 0x63 };
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 7; ++c)
            if (rows[r] & (0x40 >> c))
            {
                int X = x + c, Y = y + r;
                if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
                { u8 *p = CPix(X, Y); p[0] = 255; p[1] = 214; p[2] = 90; }
            }
}
