// ===================== Bottom screen =====================
#define BWIN_X  20
#define BWIN_Y  20

static int savedBotValid;

static FbInfo GetBotFb(int which)
{
    u32 fmt = REG32(LCD_BOT + LCD_FORMAT) & 7;
    FbInfo f;
    f.fmt    = fmt;
    f.bpp    = (fmt == 0) ? 4u : (fmt == 1) ? 3u : 2u;
    f.stride = REG32(LCD_BOT + LCD_STRIDE);
    f.fb     = REG32(LCD_BOT + (which ? LCD_FBA2 : LCD_FBA1));
    return f;
}

static void BotGrab(void)
{
    u32 sel = REG32(LCD_BOT + LCD_SELECT) & 1;
    FbInfo f = GetBotFb(sel);
    savedBotValid = 0;
    if (!f.fb) return;
    for (int y = 0; y < BOT_H; ++y)
        for (int x = 0; x < BOT_W; ++x)
        {
            volatile u8 *px = (volatile u8 *)((f.fb + (u32)x * f.stride + (u32)(BOT_H - 1 - y) * f.bpp) | (1u << 31));
            u8 r, g, b;
            if (f.bpp >= 3) { b = px[0]; g = px[1]; r = px[2]; }
            else
            {
                u16 v = *(volatile u16 *)px;
                if (f.fmt == 3)      { r = (u8)(((v >> 11) & 31) << 3); g = (u8)(((v >> 6) & 31) << 3); b = (u8)(((v >> 1) & 31) << 3); }
                else if (f.fmt == 4) { r = (u8)(((v >> 12) & 15) * 17); g = (u8)(((v >> 8) & 15) * 17); b = (u8)(((v >> 4) & 15) * 17); }
                else                 { r = (u8)(((v >> 11) & 31) << 3); g = (u8)(((v >> 5) & 63) << 2); b = (u8)((v & 31) << 3); }
            }
            savedBot[y * BOT_W + x] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    savedBotValid = 1;
}

static void BotRestoreBoth(void)
{
    if (!savedBotValid) return;
    for (int which = 0; which < 2; ++which)
    {
        FbInfo f = GetBotFb(which);
        if (!f.fb) continue;
        for (int y = 0; y < BOT_H; ++y)
            for (int x = 0; x < BOT_W; ++x)
            {
                u16 v = savedBot[y * BOT_W + x];
                u8 p[3] = { (u8)(((v >> 11) & 31) << 3), (u8)(((v >> 5) & 63) << 2), (u8)((v & 31) << 3) };
                FbWritePx(&f, x, y, p, BOT_H);
            }
    }
}

static void BotBlitComposeBoth(void)
{
    for (int which = 0; which < 2; ++which)
    {
        FbInfo f = GetBotFb(which);
        if (!f.fb) continue;
        for (int y = 0; y < BOT_H; ++y)
            for (int x = 0; x < BOT_W; ++x)
                FbWritePx(&f, x, y, CPix(x, y), BOT_H);
    }
}

static void ComposeBottom(void)
{
    for (int y = 0; y < BOT_H; ++y)
        for (int x = 0; x < BOT_W; ++x)
        {
            u8 *p = CPix(x, y);
            if (savedBotValid)
            {
                u16 v = savedBot[y * BOT_W + x];
                p[0] = (u8)(((v >> 11) & 31) << 3); p[1] = (u8)(((v >> 5) & 63) << 2); p[2] = (u8)((v & 31) << 3);
            }
            else { p[0] = p[1] = p[2] = 0; }
            if (x < BWIN_X || x >= BWIN_X + BOTBG_W || y < BWIN_Y || y >= BWIN_Y + BOTBG_H)
            { p[0] = (u8)(p[0] * 130 / 255); p[1] = (u8)(p[1] * 130 / 255); p[2] = (u8)(p[2] * 130 / 255); }
        }

    if (g_themeParchment) // Zelda Classic: parchment image
    {
        for (int y = 0; y < BOTBG_H; ++y)
        {
            const u16 *src = &botbg[y * BOTBG_W];
            u8 *p = CPix(BWIN_X, BWIN_Y + y);
            for (int x = 0; x < BOTBG_W; ++x, p += 3)
            {
                u16 v = src[x];
                p[0] = (u8)(((v >> 11) & 31) << 3);
                p[1] = (u8)(((v >> 5) & 63) << 2);
                p[2] = (u8)((v & 31) << 3);
            }
        }
    }
    else // themed: solid background + gold border
    {
        CFill(BWIN_X, BWIN_Y, BOTBG_W, BOTBG_H, BG);
        CFill(BWIN_X, BWIN_Y, BOTBG_W, 2, GOLD); CFill(BWIN_X, BWIN_Y + BOTBG_H - 2, BOTBG_W, 2, GOLD);
        CFill(BWIN_X, BWIN_Y, 2, BOTBG_H, GOLD); CFill(BWIN_X + BOTBG_W - 2, BWIN_Y, 2, BOTBG_H, GOLD);
    }

    int tx = BWIN_X + 18, ty = BWIN_Y + 14;
    CText(tx, ty, "Zelda Ocarina Of Time 3D", GOLD, 1);
    CFill(tx, ty + 17, CTextWidth("Zelda Ocarina Of Time 3D") + 8, 1, GOLD);
    CTextBtn(tx, ty + 30,  T("{DP} navigate / page"), INK, 0);
    CTextBtn(tx, ty + 48,  T("{A} open / toggle    {B} back"), INK, 0);
    CTextBtn(tx, ty + 66,  T("{X} cheat info    {Y} favorite"), INK, 0);
    CText(tx, ty + 84,  T("SELECT: close menu"), INK, 0);
    // quick-menu hint on two lines, dropped a touch below SELECT so it isn't crowded, and short enough
    // that the combo line stays left and never runs off the parchment.
    CText(tx, ty + 110, T("in-game:"), INK_DIM, 0);
    {
        char qm[64]; int i = 0;
        for (const char *s = qmCombos[qmCombo].name; *s && i < 12; ++s) qm[i++] = *s;
        qm[i++] = ' ';
        for (const char *s = T("quick menu"); *s && i < 62; ++s) qm[i++] = *s;
        qm[i] = 0;
        CTextBtn(tx, ty + 126, qm, GOLD, 0);
    }
    CText6(tx, BWIN_Y + BOTBG_H - 16, "OcarinaCTRComposer " PLUGIN_VER, INK_DIM);
}

