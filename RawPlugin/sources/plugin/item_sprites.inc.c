// ===================== Item sprites (RGBA4444, from sprites.h) =====================
static const SpriteRef *FindSprite(int key)
{
    if (key < 0) return NULL;
    for (int i = 0; i < NUM_SPRITES; ++i)
        if (sprites[i].key == key) return &sprites[i];
    return NULL;
}

static void DrawSprite(int x, int y, int key, int big)
{
    const SpriteRef *s = FindSprite(key);
    if (!s) return;
    const u16 *px = big ? s->px42 : s->px16;
    int n = big ? SPR42 : SPR16;
    for (int yy = 0; yy < n; ++yy)
        for (int xx = 0; xx < n; ++xx)
        {
            u16 v = px[yy * n + xx];
            u32 a = (u32)(v & 0xF) * 17;
            if (!a) continue;
            int X = x + xx, Y = y + yy;
            if ((unsigned)X >= TOP_W || (unsigned)Y >= TOP_H) continue;
            u8 r = (u8)(((v >> 12) & 0xF) * 17);
            u8 g = (u8)(((v >> 8) & 0xF) * 17);
            u8 b = (u8)(((v >> 4) & 0xF) * 17);
            u8 *p = CPix(X, Y);
            p[0] = (u8)((p[0] * (255 - a) + r * a) / 255);
            p[1] = (u8)((p[1] * (255 - a) + g * a) / 255);
            p[2] = (u8)((p[2] * (255 - a) + b * a) / 255);
        }
}

// Nearest-neighbour scaled blit of an arbitrary RGBA4444 sprite (sw x sh) into a dst rect (dw x dh)
// on the top-screen compose buffer, alpha-blended. dim (0..255) darkens toward black (for the
// greyed-out hex keys in DEC mode). Used by the keypad's stone-tile keys and OK/Cancel buttons.
static void DrawKbSprite(int dx, int dy, int dw, int dh, const u16 *px, int sw, int sh, int dim)
{
    for (int yy = 0; yy < dh; ++yy)
    {
        int sy = yy * sh / dh; int Y = dy + yy;
        if ((unsigned)Y >= TOP_H) continue;
        for (int xx = 0; xx < dw; ++xx)
        {
            int sx = xx * sw / dw; int X = dx + xx;
            if ((unsigned)X >= TOP_W) continue;
            u16 v = px[sy * sw + sx];
            u32 a = (u32)(v & 0xF) * 17;
            if (!a) continue;
            u8 r = (u8)(((v >> 12) & 0xF) * 17);
            u8 g = (u8)(((v >> 8) & 0xF) * 17);
            u8 b = (u8)(((v >> 4) & 0xF) * 17);
            if (dim) { r = (u8)(r * (255 - dim) / 255); g = (u8)(g * (255 - dim) / 255); b = (u8)(b * (255 - dim) / 255); }
            u8 *p = CPix(X, Y);
            p[0] = (u8)((p[0] * (255 - a) + r * a) / 255);
            p[1] = (u8)((p[1] * (255 - a) + g * a) / 255);
            p[2] = (u8)((p[2] * (255 - a) + b * a) / 255);
        }
    }
}

// Nearest-neighbour scaled draw of the 16px sprite into a dst x dst box.

#define SPRK_TRIFORCE 0x1FF // pseudo-key: golden Triforce for "unlock ALL" cheats
#define SPRK_SUNRISE  0x1F0 // hand-drawn 16px icons (not in the item sheet)
#define SPRK_DAY      0x1F1
#define SPRK_SUNSET   0x1F2
#define SPRK_NIGHT    0x1F3
#define SPRK_CLOCK    0x1F4
#define SPRK_RAIN     0x1F5
#define SPRK_CARROT   0x1F6
#define SPRK_PIN      0x1F7 // Save Position: green map pin
#define SPRK_PORTAL   0x1F8 // Warp: cyan teleport portal
#define SPRK_GIANT    0x1F9 // Giant Link:  big green figure + up chevron
#define SPRK_MINI     0x1FA // Mini Link:   small figure + down chevron
#define SPRK_NORMAL   0x1FB // Normal Size: medium figure, no arrow
#define SPRK_PAPER    0x1FC // Paper Link:  edge-on (flat) figure

// which sprite illustrates each cheat row (-1 = none)
static int SpriteKeyForCheat(int ch)
{
    switch (ch)
    {
        case CH_MOONJUMP:      return SPRK_NIGHT;   // crescent moon
        case CH_EPONA_MJ:      return SPRK_NIGHT;
        case CH_WP_SAVE:       return SPRK_PIN;
        case CH_WP_WARP:       return SPRK_PORTAL;
        case CH_EPONA_CARROTS: return SPRK_CARROT;
        case CH_EPONA_CARROTS_ALL: return SPRK_CARROT;
        case CH_T_SUNRISE:     return SPRK_SUNRISE;
        case CH_T_DAY:         return SPRK_DAY;
        case CH_T_SUNSET:      return SPRK_SUNSET;
        case CH_T_NIGHT:       return SPRK_NIGHT;
        case CH_T_MOD:         return SPRK_CLOCK;
        case CH_RAIN:          return SPRK_RAIN;
        case CH_FASTMOVE:      return 0x110; // hover boots
        case CH_INVINCIBLE:    return 0x40;  // mirror shield
        case CH_REFILL_HEART:  return 0x102;
        case CH_REFILL_MAGIC:  return 0x104;
        case CH_HEARTS_MAX:    return 0x102;
        case CH_HEARTS_KEEP:   return 0x103;
        case CH_UNLOCK_MAGIC:  return 0x104;
        case CH_UNLOCK_LMAGIC: return 0x105;
        case CH_SPIN:          return 0x111;
        case CH_SWORD_GLITCH:  return 0x3C;
        case CH_STICK_FIRE:    return 0x00;
        case CH_NAYRU:         return 0x13;
        case CH_SW_KOKIRI:     return 0x3B;
        case CH_SW_MASTER:     return 0x3C;
        case CH_SW_BIGGORON:   return 0x7C;
        case CH_SW_ALL:        return SPRK_TRIFORCE;
        case CH_SH_DEKU:       return 0x3E;
        case CH_SH_HYLIAN:     return 0x3F;
        case CH_SH_MIRROR:     return 0x40;
        case CH_SH_ALL:        return SPRK_TRIFORCE;
        case CH_SU_KOKIRI:     return 0x41;
        case CH_SU_GORON:      return 0x42;
        case CH_SU_ZORA:       return 0x43;
        case CH_SU_ALL:        return SPRK_TRIFORCE;
        case CH_ARROWS:        return 0x100;
        case CH_NUTS:          return 0x01;
        case CH_STICKS:        return 0x00;
        case CH_BOMBS:         return 0x02;
        case CH_BOMBCHU:       return 0x09;
        case CH_SLING:         return 0x101;
        case CH_EXPLOSIVES:    return 0x02;
        case CH_GAUNT_BLACK:   return 0x10E;
        case CH_GAUNT_BLUE:    return 0x10D;
        case CH_GAUNT_GREEN:   return 0x10D;
        case CH_GAUNT_PURPLE:  return 0x10E;
        case CH_BOTTLE_ALL:    return 0x14;
        case CH_ALL_ITEMS:     return SPRK_TRIFORCE;
        case CH_SKULLTULA:     return 0x106;
        case CH_RUPEES:        return 0x10F;
        case CH_Q_BEST:        return 0x10D;
        case CH_Q_STONES:      return 0x10C;
        case CH_Q_MEDAL:       return 0x10B;
        case CH_Q_DEFENSE:     return 0x3F;
        case CH_Q_HEARTS:      return 0x103;
        case CH_Q_KEYS:        return 0x107;
        case CH_Q_MAPS:        return 0x108;
        case CH_LEARN_SONGS:   return 0x008; // Ocarina of Time icon
        case CH_CHEST_MANY:    return 0x10A;
        case CH_HEARTPIECE_MANY: return 0x103;
        case CH_KNIFE_NOBREAK: return 0x3D;
        case CH_GIANT:         return SPRK_GIANT;
        case CH_MINI:          return SPRK_MINI;
        case CH_NORMAL:        return SPRK_NORMAL;
        case CH_PAPER:         return SPRK_PAPER;
        case CH_TOGGLE_AGE:    return 0x3C;      // Master Sword - the blade that ages Link
        case CH_FREEZE_TIME:   return SPRK_CLOCK;
        case CH_SKIP_SONG:     return 0x07;      // Fairy Ocarina
    }
    return -1;
}

// 16px golden Triforce (three shaded triangles) for "unlock ALL" rows
static void TriforceIcon16(int x, int y)
{
    // shadow pass then gold pass, three triangles of height 7
    static const struct { int cx, ty; } tris[3] = { {8, 0}, {4, 8}, {12, 8} };
    for (int pass = 0; pass < 2; ++pass)
        for (int t = 0; t < 3; ++t)
            for (int i = 0; i < 7; ++i)
            {
                int w = i + 1;
                int x0 = x + tris[t].cx - (w >> 1) + pass;      // pass 1 = offset shadow
                int y0 = y + tris[t].ty + i + pass;
                if (pass == 0) CFill(x0 + 1, y0 + 1, w, 1, 96, 66, 14);      // shadow
                else           CFill(x0, y0 - 1, w, 1,
                                     (u8)(255 - i * 8), (u8)(214 - i * 6), (u8)(90 - i * 4)); // gold gradient
            }
}

// ---- hand-drawn 16px icons (things the item sheet doesn't have) ----
static void CDisc(int cx, int cy, int r, u8 R, u8 G, u8 B)
{
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r)
            {
                int X = cx + dx, Y = cy + dy;
                if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
                { u8 *p = CPix(X, Y); p[0] = R; p[1] = G; p[2] = B; }
            }
}

static void DayIcon(int x, int y)
{
    CDisc(x + 8, y + 8, 4, 255, 214, 60);
    CFill(x + 8, y + 1, 1, 2, 255, 214, 60);  CFill(x + 8, y + 13, 1, 2, 255, 214, 60);
    CFill(x + 1, y + 8, 2, 1, 255, 214, 60);  CFill(x + 13, y + 8, 2, 1, 255, 214, 60);
    CFill(x + 3, y + 3, 2, 1, 255, 214, 60);  CFill(x + 11, y + 3, 2, 1, 255, 214, 60);
    CFill(x + 3, y + 12, 2, 1, 255, 214, 60); CFill(x + 11, y + 12, 2, 1, 255, 214, 60);
}

static void HalfSunIcon(int x, int y, u8 R, u8 G, u8 B) // sun on the horizon
{
    for (int dy = -4; dy <= 0; ++dy)
        for (int dx = -4; dx <= 4; ++dx)
            if (dx * dx + dy * dy <= 16)
            {
                int X = x + 8 + dx, Y = y + 11 + dy;
                if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
                { u8 *p = CPix(X, Y); p[0] = R; p[1] = G; p[2] = B; }
            }
    CFill(x + 8, y + 3, 1, 2, R, G, B);       // ray up
    CFill(x + 3, y + 5, 2, 1, R, G, B);       // rays diagonal
    CFill(x + 11, y + 5, 2, 1, R, G, B);
    CFill(x + 1, y + 12, 14, 1, (u8)(R * 3 / 4), (u8)(G * 3 / 4), (u8)(B * 3 / 4)); // horizon
}

static void NightIcon(int x, int y)
{
    for (int dy = -5; dy <= 5; ++dy)
        for (int dx = -5; dx <= 5; ++dx)
        {
            if (dx * dx + dy * dy > 25) continue;
            int ox = dx - 3, oy = dy + 2;              // carve an offset disc -> crescent
            if (ox * ox + oy * oy <= 20) continue;
            int X = x + 8 + dx, Y = y + 8 + dy;
            if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
            { u8 *p = CPix(X, Y); p[0] = 240; p[1] = 240; p[2] = 200; }
        }
    CFill(x + 3, y + 3, 1, 1, 255, 244, 180); // stars
    CFill(x + 5, y + 1, 1, 1, 255, 244, 180);
}

static void ClockIcon(int x, int y)
{
    for (int dy = -6; dy <= 6; ++dy)
        for (int dx = -6; dx <= 6; ++dx)
        {
            int rr = dx * dx + dy * dy;
            if (rr > 36 || rr < 25) continue;
            int X = x + 8 + dx, Y = y + 8 + dy;
            if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
            { u8 *p = CPix(X, Y); p[0] = 236; p[1] = 200; p[2] = 120; }
        }
    CFill(x + 8, y + 4, 1, 4, 248, 240, 216); // hands
    CFill(x + 8, y + 8, 3, 1, 248, 240, 216);
}

static void RainIcon(int x, int y)
{
    CDisc(x + 5, y + 6, 3, 208, 212, 222);
    CDisc(x + 9, y + 5, 3, 208, 212, 222);
    CDisc(x + 11, y + 7, 2, 208, 212, 222);
    CFill(x + 3, y + 6, 11, 3, 208, 212, 222);
    CFill(x + 4, y + 11, 1, 3, 110, 170, 240);
    CFill(x + 8, y + 12, 1, 3, 110, 170, 240);
    CFill(x + 12, y + 11, 1, 3, 110, 170, 240);
}

static void CarrotIcon(int x, int y)
{
    for (int i = 0; i < 10; ++i)
    {
        int w = 5 - i / 2;
        if (w < 1) w = 1;
        CFill(x + 8 - w / 2, y + 5 + i, w, 1, 240, 122, 30);
    }
    CFill(x + 6, y + 2, 2, 3, 70, 170, 60);  // leaves
    CFill(x + 9, y + 2, 2, 3, 70, 170, 60);
    CFill(x + 8, y + 3, 1, 2, 96, 200, 80);
}

// draws the icon of a cheat row (sprite or special glyph)
// Save Position: a map pin (round head + tapering point + hole), green = "mark a spot".
static void PinIcon(int x, int y)
{
    CDisc(x + 8, y + 5, 4, 92, 202, 112);            // round head
    for (int i = 0; i < 7; ++i)                       // taper down to a point
    {
        int w = 7 - i; if (w < 1) w = 1;
        CFill(x + 8 - w / 2, y + 8 + i, w, 1, 92, 202, 112);
    }
    CDisc(x + 8, y + 5, 1, 26, 40, 30);               // hole in the head
}
// Warp: a teleport portal - two concentric rings + a bright core, cyan = "go there".
static void PortalIcon(int x, int y)
{
    for (int dy = -7; dy <= 7; ++dy)
        for (int dx = -7; dx <= 7; ++dx)
        {
            int rr = dx * dx + dy * dy;
            if (!((rr <= 49 && rr >= 32) || (rr <= 16 && rr >= 6))) continue; // outer + inner ring
            int X = x + 8 + dx, Y = y + 8 + dy;
            if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
            { u8 *p = CPix(X, Y); p[0] = 96; p[1] = 196; p[2] = 236; }
        }
    CDisc(x + 8, y + 8, 1, 210, 244, 255);            // bright core
}

// ---- size-modifier figures (Giant / Mini / Normal / Paper Link) ----
// A tiny green-tunic Link silhouette: pointed cap, skin head, triangular tunic, two boots.
// Centered on x+8, spanning rows [top,bot]; hw = tunic half-width at the hem.
static void TunicFig(int x, int y, int top, int bot, int hw)
{
    int cx = x + 8;
    int span = bot - top;
    int headR = (span >= 9) ? 2 : 1;
    int hcy = y + top + headR + 1;                     // head center (leave a row for the cap tip)
    CFill(cx, y + top, 1, 2, 46, 132, 60);             // cap tip
    CDisc(cx, hcy, headR, 245, 205, 150);              // head
    int bodyTop = hcy + headR;
    int hem = bot - 2;
    for (int yy = bodyTop; yy <= hem; ++yy)            // tunic triangle, widening to the hem
    {
        int prog = (hem > bodyTop) ? (yy - bodyTop) * hw / (hem - bodyTop) : hw;
        int w = 1 + prog;
        CFill(cx - w, y + yy, 2 * w + 1, 1, 56, 158, 72);
    }
    CFill(cx - hw, y + hem + 1, 2, bot - hem, 120, 82, 44);       // left boot
    CFill(cx + hw - 1, y + hem + 1, 2, bot - hem, 120, 82, 44);   // right boot
}
static void UpChevron(int x, int y)   // small gold up-arrow (grow)
{
    CFill(x + 8, y, 1, 1, 236, 200, 120);
    CFill(x + 7, y + 1, 3, 1, 236, 200, 120);
    CFill(x + 6, y + 2, 5, 1, 236, 200, 120);
}
static void DownChevron(int x, int y) // small gold down-arrow (shrink)
{
    CFill(x + 6, y, 5, 1, 236, 200, 120);
    CFill(x + 7, y + 1, 3, 1, 236, 200, 120);
    CFill(x + 8, y + 2, 1, 1, 236, 200, 120);
}
static void GiantIcon(int x, int y)  { UpChevron(x, y);       TunicFig(x, y, 4, 15, 4); } // towering + grow arrow
static void MiniIcon(int x, int y)   { DownChevron(x, y + 3); TunicFig(x, y, 7, 15, 2); } // tiny + shrink arrow (arrow tucked in)
static void NormalIcon(int x, int y) { TunicFig(x, y, 2, 14, 3); }                        // medium, unchanged
// Paper Link: the whole figure flattened to a thin sliver, with a lit edge that catches the light.
static void PaperIcon(int x, int y)
{
    int cx = x + 8;
    CFill(cx, y + 1, 1, 2, 46, 132, 60);           // cap tip
    CDisc(cx, y + 4, 1, 245, 205, 150);            // thin head
    CFill(cx - 1, y + 6, 2, 7, 56, 158, 72);       // flattened body (~3px thick)
    CFill(cx + 1, y + 6, 1, 7, 110, 200, 120);     // lit sheet edge (flush with the body)
    CFill(cx - 1, y + 13, 3, 2, 120, 82, 44);      // boots
}
static void DrawCheatIcon(int x, int y, int ch)
{
    int sk = SpriteKeyForCheat(ch);
    switch (sk)
    {
        case SPRK_TRIFORCE: TriforceIcon16(x, y); return;
        case SPRK_SUNRISE:  HalfSunIcon(x, y, 255, 220, 90);  return;
        case SPRK_DAY:      DayIcon(x, y); return;
        case SPRK_SUNSET:   HalfSunIcon(x, y, 255, 140, 50);  return;
        case SPRK_NIGHT:    NightIcon(x, y); return;
        case SPRK_CLOCK:    ClockIcon(x, y); return;
        case SPRK_RAIN:     RainIcon(x, y); return;
        case SPRK_CARROT:   CarrotIcon(x, y); return;
        case SPRK_PIN:      PinIcon(x, y); return;
        case SPRK_PORTAL:   PortalIcon(x, y); return;
        case SPRK_GIANT:    GiantIcon(x, y); return;
        case SPRK_MINI:     MiniIcon(x, y); return;
        case SPRK_NORMAL:   NormalIcon(x, y); return;
        case SPRK_PAPER:    PaperIcon(x, y); return;
    }
    if (sk >= 0) DrawSprite(x, y, sk, 0);
}
