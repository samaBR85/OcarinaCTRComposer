// ===================== RAM compose buffer (RGB888, row-major, heap) =====================
static u8  *gCompose;  // TOP_W * TOP_H * 3
static u16 *savedBot;  // BOT_W * BOT_H (RGB565 backup of the game's bottom frame)
static u16 *savedTop;  // TOP_W * TOP_H (RGB565 backup of the dimmed top backdrop)
static int  savedTopValid;

static inline u8 *CPix(int x, int y) { return &gCompose[(y * TOP_W + x) * 3]; }

static void CFill(int x0, int y0, int w, int h, u8 r, u8 g, u8 b)
{
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > TOP_W) w = TOP_W - x0;
    if (y0 + h > TOP_H) h = TOP_H - y0;
    for (int y = y0; y < y0 + h; ++y)
    {
        u8 *p = CPix(x0, y);
        for (int x = 0; x < w; ++x, p += 3) { p[0] = r; p[1] = g; p[2] = b; }
    }
}

static void CFillBlend(int x0, int y0, int w, int h, u8 r, u8 g, u8 b, u8 a)
{
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > TOP_W) w = TOP_W - x0;
    if (y0 + h > TOP_H) h = TOP_H - y0;
    for (int y = y0; y < y0 + h; ++y)
    {
        u8 *p = CPix(x0, y);
        for (int x = 0; x < w; ++x, p += 3)
        {
            p[0] = (u8)((p[0] * (255 - a) + r * a) / 255);
            p[1] = (u8)((p[1] * (255 - a) + g * a) / 255);
            p[2] = (u8)((p[2] * (255 - a) + b * a) / 255);
        }
    }
}

// The 6x10 font is ASCII-only, so the small font strips accents to the base
// letter (é->e, ç->c). Only the Cheat Search form / hints / footers use it.
static unsigned char SmallAscii(u32 code)
{
    if (code < 0x80) return (unsigned char)code;
    switch (code)
    {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5: return 'A';
        case 0x00C7: return 'C';
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: return 'E';
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: return 'I';
        case 0x00D1: return 'N';
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: return 'O';
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: return 'U';
        case 0x00DF: return 's';
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5: return 'a';
        case 0x00E7: return 'c';
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: return 'e';
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: return 'i';
        case 0x00F1: return 'n';
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: return 'o';
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: return 'u';
        case 0x00AA: return 'a';   case 0x00BA: return 'o';
        case 0x00A1: return '!';   case 0x00BF: return '?';
        case 0x00AB: return '<';   case 0x00BB: return '>';
        default: return 0;         // undrawable: skip
    }
}

static void CText6(int x, int y, const char *s, u8 r, u8 g, u8 b)
{
    while (*s)
    {
        unsigned char ch = SmallAscii(SysFontUtf8Next(&s));
        if (!ch) continue;
        const unsigned char *glyph = &font[ch * FONT_HEIGHT];
        for (int dy = 0; dy < FONT_HEIGHT; ++dy)
            for (int dx = 0; dx < FONT_WIDTH; ++dx)
                if (glyph[dy] & (0x80 >> dx))
                {
                    int X = x + dx, Y = y + dy;
                    if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
                    { u8 *p = CPix(X, Y); p[0] = r; p[1] = g; p[2] = b; }
                }
        x += FONT_WIDTH + 1;
    }
}

// 6x10 scaled 1.5x — fallback if the system font is unavailable
static void CText15(int x, int y, const char *s, u8 r, u8 g, u8 b)
{
    for (; *s; ++s)
    {
        const unsigned char *glyph = &font[(unsigned char)*s * FONT_HEIGHT];
        for (int dy = 0; dy < 15; ++dy)
        {
            unsigned char bits = glyph[dy * 2 / 3];
            for (int dx = 0; dx < 9; ++dx)
                if (bits & (0x80 >> (dx * 2 / 3)))
                {
                    int X = x + dx, Y = y + dy;
                    if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
                    { u8 *p = CPix(X, Y); p[0] = r; p[1] = g; p[2] = b; }
                }
        }
        x += 10;
    }
}

static void CText(int x, int y, const char *s, u8 r, u8 g, u8 b, int bold)
{
    if (SysFontReady()) SysFontDrawText(gCompose, x, y, s, r, g, b, bold);
    else                CText15(x, y + 1, s, r, g, b);
}
static int CTextWidth(const char *s)
{
    if (SysFontReady()) return SysFontTextWidth(s);
    int n = 0; while (s[n]) n++;
    return n * 10;
}

// Draw text, truncating with ".." if it would exceed maxw pixels.
static void CTextClip(int x, int y, const char *s, int maxw, u8 r, u8 g, u8 b, int bold)
{
    char buf[72];
    int n = 0;
    while (s[n] && n < 70) { buf[n] = s[n]; n++; }
    buf[n] = 0;
    if (CTextWidth(buf) <= maxw) { CText(x, y, buf, r, g, b, bold); return; }
    while (n > 1)
    {
        buf[--n] = 0;
        char tmp[74];
        int t = 0;
        for (int i = 0; i < n; ++i) tmp[t++] = buf[i];
        tmp[t++] = '.'; tmp[t++] = '.'; tmp[t] = 0;
        if (CTextWidth(tmp) <= maxw) { CText(x, y, tmp, r, g, b, bold); return; }
    }
    CText(x, y, "..", r, g, b, bold);
}

// ===================== Framebuffer <-> compose =====================
typedef struct { u32 fb; u32 stride; u32 bpp; u32 fmt; } FbInfo;

static FbInfo GetFb(int hidden)
{
    u32 fmt = REG32(LCD_TOP + LCD_FORMAT) & 7;
    u32 sel = REG32(LCD_TOP + LCD_SELECT) & 1;
    FbInfo f;
    f.fmt    = fmt;
    f.bpp    = (fmt == 0) ? 4u : (fmt == 1) ? 3u : 2u;
    f.stride = REG32(LCD_TOP + LCD_STRIDE);
    if (hidden) f.fb = REG32(LCD_TOP + (sel ? LCD_FBA1 : LCD_FBA2));
    else        f.fb = REG32(LCD_TOP + (sel ? LCD_FBA2 : LCD_FBA1));
    return f;
}

static inline void FbWritePx(const FbInfo *f, int x, int y, const u8 *p, int screenH)
{
    volatile u8 *px = (volatile u8 *)((f->fb + (u32)x * f->stride + (u32)(screenH - 1 - y) * f->bpp) | (1u << 31));
    if (f->bpp >= 3) { px[0] = p[2]; px[1] = p[1]; px[2] = p[0]; if (f->bpp == 4) px[3] = 0xFF; }
    else
    {
        u16 v;
        if (f->fmt == 3)      v = (u16)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) | 1);
        else if (f->fmt == 4) v = (u16)(((p[0] >> 4) << 12) | ((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) | 0xF);
        else                 v = (u16)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
        *(volatile u16 *)px = v;
    }
}

static void BlitTopRect(const FbInfo *f, int x0, int y0, int w, int h)
{
    if (!f->fb) return;
    for (int x = x0; x < x0 + w; ++x)
        for (int y = y0; y < y0 + h; ++y)
            FbWritePx(f, x, y, CPix(x, y), TOP_H);
}

// Which buffer the top screen was displaying when we took over. Present() flips this register to
// show OUR frame; nothing else ever puts it back, so the plugin has to.
static u32 g_lcdSelSaved = 0;
static int g_lcdSelValid = 0;

// Call when the overlay takes the top screen, BEFORE the first Present().
static void TopTakeOver(void)
{
    if (g_lcdSelValid) return;              // nested (quick menu -> menu): keep the outermost save
    g_lcdSelSaved = REG32(LCD_TOP + LCD_SELECT) & 1;
    g_lcdSelValid = 1;
}

// Call when handing the screen back to the game, right before ResumeGame().
//
// WHY THIS EXISTS: the bottom screen draws straight into the visible buffer, so restoring its
// pixels is enough. The top does NOT - Present() writes the hidden buffer and flips this register.
// Leave it flipped and the LCD keeps scanning out our frame: the game runs (bottom screen returns,
// audio plays) while the top stays frozen on the last menu frame.
static void TopRelease(void)
{
    if (!g_lcdSelValid) return;
    REG32(LCD_TOP + LCD_SELECT) = g_lcdSelSaved;
    g_lcdSelValid = 0;
}

static void Present(void)
{
    u32 sel = REG32(LCD_TOP + LCD_SELECT) & 1;
    FbInfo f = GetFb(1);
    if (!f.fb) return;
    BlitTopRect(&f, 0, 0, TOP_W, TOP_H);
    REG32(LCD_TOP + LCD_SELECT) = sel ^ 1;
}

static void GrabFb(void)
{
    FbInfo f = GetFb(0);
    if (!f.fb) { memset(gCompose, 0, TOP_W * TOP_H * 3); return; }
    for (int y = 0; y < TOP_H; ++y)
        for (int x = 0; x < TOP_W; ++x)
        {
            u32 off = (u32)x * f.stride + (u32)(TOP_H - 1 - y) * f.bpp;
            volatile u8 *px = (volatile u8 *)((f.fb + off) | (1u << 31));
            u8 r, g, b;
            if (f.bpp >= 3) { b = px[0]; g = px[1]; r = px[2]; }
            else
            {
                u16 v = *(volatile u16 *)px;
                if (f.fmt == 3)      { r = (u8)(((v >> 11) & 31) << 3); g = (u8)(((v >> 6) & 31) << 3); b = (u8)(((v >> 1) & 31) << 3); }
                else if (f.fmt == 4) { r = (u8)(((v >> 12) & 15) * 17); g = (u8)(((v >> 8) & 15) * 17); b = (u8)(((v >> 4) & 15) * 17); }
                else                 { r = (u8)(((v >> 11) & 31) << 3); g = (u8)(((v >> 5) & 63) << 2); b = (u8)((v & 31) << 3); }
            }
            u8 *p = CPix(x, y);
            p[0] = r; p[1] = g; p[2] = b;
        }
}
