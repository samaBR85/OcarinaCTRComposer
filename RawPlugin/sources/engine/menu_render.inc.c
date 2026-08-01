// ===================== Menu rendering =====================
// ---- 13px themed category icons (gold) ----
#define GLD2 224, 186, 96
#define GLDK 130, 92, 20
// Pick the icon for a HOME category folder; fall back to the generic folder.
static void CategoryIcon(int folderId, int x, int y)
{
    switch (folderId)
    {
        case F_MOVEMENT:  DrawSprite(x, y - 1, 0x110, 0); break; // hover boots
        case F_BATTLE:    DrawSprite(x, y - 1, 0x3C,  0); break; // Master Sword
        case F_INVENTORY: DrawSprite(x, y - 1, 0x10F, 0); break; // adventurer's pouch
        case F_TIME:      DrawSprite(x, y - 1, 0x08,  0); break; // Ocarina of Time (wordplay: "of Time")
        case F_QUEST:     DrawSprite(x, y - 1, 0x10A, 0); break; // Boss Key
        case F_MISC:      DrawSprite(x, y - 1, 0x2B,  0); break; // Mask of Truth
        case F_TOOLS:     DrawSprite(x, y - 1, 0x11,  0); break; // Megaton Hammer
        case F_SETTINGS:  DrawSprite(x, y - 1, 0x0F,  0); break; // Lens of Truth (inspect/adjust)
        case F_TELEPORT:  DrawSprite(x, y - 1, 0x0D,  0); break; // Farore's Wind (the game's teleport spell)
        default:          FolderIconSmall(x, y + 1); break;
    }
}

// Sprite key for a tool's row icon (shared by the main menu and the quick menu).
static int ToolSprite(int tool)
{
    switch (tool)
    {
        case T_GAMEGUIDE:   return 0x108; // Dungeon Map   - a map guides you
        case T_PLUGINGUIDE: return 0x109; // Compass       - points the way (pairs with the map)
        case T_CHECKLIST:   return 0x112; // Gerudo crown  - 100% / achievement
        case T_SEARCH:      return 0x0F;  // Lens of Truth - reveal a hidden value
        case T_RAMDUMP:     return 0x14;  // Bottle        - capture memory to a file
        case T_HEXEDIT:     return 0x11;  // Megaton Hammer - tinker with bytes
        case T_ABOUT:       return 0x23;  // Zelda's Letter - info / credits
        default:            return 0x107; // key icon = generic utility/tool
    }
}

// Draw one menu row/cell at (x,y) spanning cellW. Handles every item type.
static void DrawMenuItem(const Item *it, int x, int y, int cellW, int selected)
{
    if (IS_SEP(it)) // non-selectable section header (dim label + hairline rule), for single-column folders
    {
        const char *sec = T(it->label);
        CText6(x, y + 3, sec, 150, 140, 112);
        int lx = x + C6Width(sec) + 6;
        CFill(lx, y + 6, (x + cellW) - lx, 1, 120, 98, 50);
        return;
    }
    if (selected)
    {
        CFillBlend(x - 4, y - 1, cellW + 8, ROW_H, 0, 0, 0, 110);
        CFill(x - 4, y - 1, 2, ROW_H, GOLD);
    }
    if (it->folder >= 0)
    {
        CategoryIcon(it->folder, x, y);
        CTextClip(x + 20, y - 1, T(it->label), cellW - 26 - (folderFav[it->folder] ? 12 : 0), INK, 0);
        if (folderFav[it->folder]) StarIcon(x + cellW - 12, y + 3);
    }
    else if (it->warp == -2) // Teleport category filter row (cycles with A, like the Theme/Language rows)
    {
        DrawSprite(x, y - 1, 0x0F, 0); // Lens of Truth = filter / inspect
        const char *m = g_tpFilter == 1 ? T("Overworld") : g_tpFilter == 2 ? T("Dungeons") : T("All");
        int cw = CTextWidth(m);
        CText(x + cellW - 6 - cw, y - 1, m, GREEN_ON, 0);
        CTextClip(x + 20, y - 1, T("Filter"), cellW - 32 - cw, INK, 0);
    }
    else if (it->warp >= 0) // teleport destination: map-pin icon + name (from warps[])
    {
        if (it->warp == 0) DrawSprite(x, y - 1, 0x0E, 0); // Reload = Boomerang (it comes back to you)
        else               PinIcon(x, y - 1);             // vector icon - DrawSprite can't do SPRK_ ids
        CTextClip(x + 20, y - 1, T(warps[it->warp].name), cellW - 26 - (warpFav[it->warp] ? 12 : 0), INK, 0);
        if (warpFav[it->warp]) StarIcon(x + cellW - 12, y + 3);
    }
    else if (it->picker >= 0)
    {
        const Picker *pk = &pickers[it->picker];
        u8 cur = R8(pk->addr);
        if (FindSprite(cur)) DrawSprite(x, y - 1, cur, 0); // current content icon
        else                 BottleIcon(x + 1, y + 1);
        for (int k = 0; k < pk->count; ++k)
            if (pk->opts[k].val == cur)
            {
                int cw = CTextWidth(T(pk->opts[k].name));
                CText(x + cellW - 6 - cw, y - 1, T(pk->opts[k].name), GREEN_ON, 0);
                CTextClip(x + 20, y - 1, T(it->label), cellW - 32 - cw, INK, 0);
                goto pickdone;
            }
        CTextClip(x + 20, y - 1, T(it->label), cellW - 26, INK, 0);
        pickdone:;
    }
    else if (it->tool >= 0)
    {
        int fav = toolFav[it->tool], rpad = fav ? 14 : 0;
        DrawSprite(x, y - 1, ToolSprite(it->tool), 0);
        CTextClip(x + 20, y - 1, T(it->label), cellW - 26 - rpad, GOLD, 0);
        if (fav) StarIcon(x + cellW - 12, y + 3);
    }
    else if (it->cheat == CH_CFG_QMKEY)
    {
        DrawSprite(x, y - 1, 0x107, 0);
        int cw = CTextBtnWidth(qmCombos[qmCombo].name); // button combo: L/R shown as glyphs
        CTextBtn(x + cellW - 6 - cw, y - 1, qmCombos[qmCombo].name, GREEN_ON, 0);
        CTextClip(x + 20, y - 1, T(it->label), cellW - 32 - cw, INK, 0);
    }
    else if (it->cheat == CH_CFG_MJKEY || it->cheat == CH_CFG_FMKEY)
    {
        DrawSprite(x, y - 1, 0x107, 0);
        const char *g = hotKeys[it->cheat == CH_CFG_MJKEY ? mjKey : fmKey].glyph;
        int cw = CTextBtnWidth(g);
        CTextBtn(x + cellW - 6 - cw, y - 1, g, GREEN_ON, 0);
        CTextClip(x + 20, y - 1, T(it->label), cellW - 32 - cw, INK, 0);
    }
    else if (it->cheat == CH_CFG_HKRESET) // an action row: icon + label, no value/checkbox
    {
        DrawSprite(x, y - 1, 0x107, 0);
        CTextClip(x + 20, y - 1, T(it->label), cellW - 26, GOLD, 0);
    }
    else if (it->cheat == CH_TOGGLE_AGE) // self-disabling action: label = the age you'll BECOME
    {
        int adult = LinkPtr() && R32(SAVE_LINKAGE) == 0;            // current VISIBLE age (gSaveContext.linkAge)
        int on = (flashCheat == CH_TOGGLE_AGE);                      // only lit for the brief post-press blink
        int fav = favorite[it->cheat];
        CheckBoxIcon(x, y + 1, on);
        DrawSprite(x + 17, y - 1, adult ? 0x3B : 0x3C, 0); // dynamic: Kokiri Sword (->child) / Master Sword (->adult)
        CTextClipBtn(x + 37, y - 1, adult ? T("Child Link") : T("Adult Link"), cellW - 53 - (fav ? 12 : 0), RGB3(on ? CGREEN : CINK), 0);
        if (fav) StarIcon(x + cellW - 12, y + 3);
    }
    else if (it->cheat == CH_WP_SLOT) // cycler: reticle + the active slot's auto-name ("N: Area")
    {
        int fav = favorite[it->cheat], rpad = fav ? 14 : 0; // leave room for the star
        DrawSprite(x, y - 1, 0x11A, 0); // Z-target reticle - distinct from Save Position's pin
        char nm[48]; WpName(g_wpSlot, nm, sizeof nm); // "Slot N" if empty, else "N: <area>"
        CTextClip(x + 20, y - 1, nm, cellW - 26 - rpad, INK, 0);
        if (fav) StarIcon(x + cellW - 12, y + 3);
    }
    else if (it->cheat == CH_WP_WARP) // action row (reloads the scene): portal icon + label
    {
        int fav = favorite[it->cheat];
        DrawCheatIcon(x, y - 1, CH_WP_WARP); // cyan portal
        CTextClip(x + 20, y - 1, T(it->label), cellW - 26 - (fav ? 12 : 0), GOLD, 0);
        if (fav) StarIcon(x + cellW - 12, y + 3);
    }
    else if (it->cheat == CH_CFG_THEME)
    {
        DrawSprite(x, y - 1, 0x107, 0);
        int cw = CTextWidth(THEMES[g_themeIdx].name); // theme name: proper noun, not translated
        CText(x + cellW - 6 - cw, y - 1, THEMES[g_themeIdx].name, GREEN_ON, 0);
        CTextClip(x + 20, y - 1, T(it->label), cellW - 32 - cw, INK, 0);
    }
    else if (it->cheat == CH_CFG_LANG)
    {
        DrawSprite(x, y - 1, 0x107, 0);
        int cw = CTextWidth(kLangLabels[g_langIdx]); // language name: shown natively, not translated
        // red when this language has no SD file (falls back to English) - a dev-phase availability cue
        if (g_langAvail[g_langIdx]) CText(x + cellW - 6 - cw, y - 1, kLangLabels[g_langIdx], GREEN_ON, 0);
        else                        CText(x + cellW - 6 - cw, y - 1, kLangLabels[g_langIdx], 225, 60, 45, 0);
        CTextClip(x + 20, y - 1, T(it->label), cellW - 32 - cw, INK, 0);
    }
    else if (it->cheat == CH_CFG_TOAST || it->cheat == CH_CFG_AUTOFILL) // settings toggles: no cheat icon, align label with the other Settings rows (+20)
    {
        int on = cheatState[it->cheat] || flashCheat == it->cheat;
        CheckBoxIcon(x, y + 1, on);
        CTextClipBtn(x + 20, y - 1, T(it->label), cellW - 26, RGB3(on ? CGREEN : CINK), 0);
    }
    else
    {
        int on = cheatState[it->cheat] || flashCheat == it->cheat;
        CheckBoxIcon(x, y + 1, on);
        DrawCheatIcon(x + 17, y - 1, it->cheat);
        CTextClipBtn(x + 37, y - 1, T(it->label), cellW - 53, RGB3(on ? CGREEN : CINK), 0);
        if (flashCheat == it->cheat)
        {
            int fx = x + cellW - 6 - C6Width(flashMsg);
            if (flashMsg[0] == 'R') CText6(fx, y + 2, flashMsg, GOLD);       // REMOVED -> gold
            else                    CText6(fx, y + 2, flashMsg, GREEN_ON);   // ADDED / OK -> green
        }
        else if (favorite[it->cheat]) StarIcon(x + cellW - 12, y + 3);
    }
}

// HOME two-column grid layout: interleaves non-selectable section headers with
// pairs of items. Each item gets a (y, col); separators get a header row.
#define RL_MAX  40
#define HDR_H   14
static int g_rlY[RL_MAX], g_rlCol[RL_MAX], g_rlSep[RL_MAX], g_rlHid[RL_MAX];
static void BuildRootLayout(const Folder *fld)
{
    int folderIdx = (int)(fld - folders);
    int y = ROW_Y0, col = 0;
    for (int i = 0; i < fld->count && i < RL_MAX; ++i)
    {
        if (ItemHidden(folderIdx, &fld->items[i])) // filtered out: takes no grid slot
        { g_rlHid[i] = 1; g_rlSep[i] = 0; g_rlCol[i] = -1; g_rlY[i] = y; continue; }
        g_rlHid[i] = 0;
        if (IS_SEP(&fld->items[i]))
        {
            if (col != 0) { y += ROW_H; col = 0; } // finish an open pair row
            g_rlSep[i] = 1; g_rlCol[i] = -1; g_rlY[i] = y;
            y += HDR_H;
        }
        else if (fld->items[i].wide)
        {
            if (col != 0) { y += ROW_H; col = 0; } // finish an open pair row
            g_rlSep[i] = 0; g_rlCol[i] = -2; g_rlY[i] = y; // -2 = spans both columns, still selectable
            y += ROW_H;
        }
        else
        {
            g_rlSep[i] = 0; g_rlCol[i] = col; g_rlY[i] = y;
            if (++col == 2) { col = 0; y += ROW_H; }
        }
    }
}
// first / next selectable item index (skip separators and filtered-out rows)
static int RootFirstSel(const Folder *fld)
{
    int folderIdx = (int)(fld - folders);
    for (int i = 0; i < fld->count; ++i) if (!IS_SEP(&fld->items[i]) && !ItemHidden(folderIdx, &fld->items[i])) return i;
    return 0;
}
// grid neighbor of `cur` in a direction (0 up,1 down,2 left,3 right), skipping
// separators; up/down wrap within the same column. Needs BuildRootLayout first.
static int RootNeighbor(const Folder *fld, int cur, int dir)
{
    int cy = g_rlY[cur], cc = g_rlCol[cur]; // cc: 0/1 normal column, -2 = wide (spans both)
    if (dir == 2 || dir == 3)
    {
        if (cc < 0) return cur; // wide row: no left/right target within the same row
        int want = (dir == 3) ? 1 : 0;
        if (cc == want) return cur;
        for (int i = 0; i < fld->count; ++i)
            if (!g_rlSep[i] && !g_rlHid[i] && g_rlY[i] == cy && g_rlCol[i] == want) return i;
        return cur;
    }
    // Column filter: a normal column only matches its own column or a wide row (which spans
    // both); a wide row (cc<0) matches everything, so up/down passes through it either way.
    int best = -1, bestY = (dir == 1) ? 0x7fffffff : -1;
    for (int i = 0; i < fld->count; ++i)
    {
        if (g_rlSep[i] || g_rlHid[i]) continue;
        if (cc >= 0 && g_rlCol[i] >= 0 && g_rlCol[i] != cc) continue;
        int yy = g_rlY[i];
        if (dir == 1 && yy > cy && yy < bestY) { bestY = yy; best = i; }
        if (dir == 0 && yy < cy && yy > bestY) { bestY = yy; best = i; }
    }
    if (best >= 0) return best;
    int ext = (dir == 1) ? 0x7fffffff : -1, extI = cur;
    for (int i = 0; i < fld->count; ++i) // wrap to the far end of this column
    {
        if (g_rlSep[i] || g_rlHid[i]) continue;
        if (cc >= 0 && g_rlCol[i] >= 0 && g_rlCol[i] != cc) continue;
        int yy = g_rlY[i];
        if (dir == 1 && yy < ext) { ext = yy; extI = i; }
        if (dir == 0 && yy > ext) { ext = yy; extI = i; }
    }
    return extI;
}

static void ComposeMenu(const Folder *fld, int depth, int cursor, int scroll)
{
    ComposeBackdrop();

    int tw = CTextWidth(T(fld->title));
    CText(WIN_X + 12, WIN_Y + 7, T(fld->title), INK, 1);
    CFill(WIN_X + 12, WIN_Y + 24, tw + 6, 1, GOLD);

    CText6(WIN_X + WIN_W - 12 - C6Width(PLUGIN_TAG), WIN_Y + 8, PLUGIN_TAG, INK_DIM);

    // HOME and Teleport use a grouped two-column grid. HOME fits in one screen (scroll==0); the
    // Teleport list can overflow, so `scroll` is a pixel offset here and rows are clipped/arrowed.
    int twoCol = (fld == &folders[F_ROOT] || fld == &folders[F_TELEPORT]);
    if (twoCol)
    {
        BuildRootLayout(fld);
        int colW = ROW_W / 2;
        int visBot = WIN_Y + WIN_H - 22; // bottom of the content band (above the footer)
        int maxY = ROW_Y0;
        for (int i = 0; i < fld->count; ++i)
        {
            if (g_rlHid[i]) continue;
            int rowH = g_rlSep[i] ? HDR_H : ROW_H;
            if (g_rlY[i] + rowH > maxY) maxY = g_rlY[i] + rowH;
            int dy = g_rlY[i] - scroll;
            if (dy < ROW_Y0 - 2 || dy + rowH > visBot + 2) continue; // clip to the visible band
            if (g_rlSep[i])
            {
                const char *sec = T(fld->items[i].label);
                CText6(ROW_X, dy + 3, sec, 150, 140, 112); // small dim label
                int lx = ROW_X + C6Width(sec) + 6;
                CFill(lx, dy + 6, (WIN_X + WIN_W - 14) - lx, 1, 120, 98, 50); // hairline rule
            }
            else if (g_rlCol[i] == -2) // wide row: full row width, no column offset
                DrawMenuItem(&fld->items[i], ROW_X, dy, ROW_W - 8, i == cursor);
            else
                DrawMenuItem(&fld->items[i], ROW_X + g_rlCol[i] * colW, dy, colW - 8, i == cursor);
        }
        if (scroll > 0) // up arrow
            for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, ROW_Y0 + 3 + a, 1 + 2 * a, 1, GOLD);
        if (maxY - scroll > visBot) // down arrow
            for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, visBot - 4 - a, 1 + 2 * a, 1, GOLD);
    }
    else
    {
        // scroll is in VISIBLE rows (hidden items compacted away). Identical to raw indexing for
        // folders with nothing hidden, so only the Teleport filter changes behaviour here.
        int folderIdx = (int)(fld - folders);
        int vp = 0;
        for (int i = 0; i < fld->count; ++i)
        {
            if (ItemHidden(folderIdx, &fld->items[i])) continue;
            if (vp >= scroll && vp < scroll + MAX_ROWS)
                DrawMenuItem(&fld->items[i], ROW_X, ROW_Y0 + (vp - scroll) * ROW_H, ROW_W, i == cursor);
            vp++;
        }
        int vis = vp;
        if (scroll > 0)
            for (int i = 0; i < 4; ++i) CFill(WIN_X + WIN_W - 14 - i, ROW_Y0 + 3 + i, 1 + 2 * i, 1, GOLD);
        if (scroll + MAX_ROWS < vis)
            for (int i = 0; i < 4; ++i) CFill(WIN_X + WIN_W - 14 - i, ROW_Y0 + MAX_ROWS * ROW_H - 4 - i, 1 + 2 * i, 1, GOLD);
    }

    (void)depth;
    CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 16, T("{X} info   {Y} fav"), INK_DIM);
}

// Word-wrapped info overlay (Gen6-style "(i)" note). Any button closes it.
// Global "exit straight to the game" request. Any sub-loop (tool, picker,
// info box) sets this on SELECT; RunMenu sees it and unwinds all the way out
// instead of just backing up one level. The menu position is kept in the
// menu* statics, so the next SELECT reopens exactly where you left off.
static int g_quitToGame = 0;
// If we exited to the game from inside a tool, remember which one so the next
// SELECT jumps straight back into it (not the Tools folder). -1 = none.
static int g_resumeTool = -1;
// Set when RunMenu() is entered straight from the quick menu. It means the game has NOT drawn a
// frame since we last painted the screen, so GrabFb() would capture our own quick-menu panel and
// bake it into the backdrop - the menu then renders over a photo of itself, and every reopen
// stacks another copy.
static int g_qmHandoff = 0;

static void InfoBox(const Item *it)
{
    // teleport rows carry their text in warps[]; everything else uses the Item's own label/desc
    const char *ibLabel = (it->warp >= 0) ? warps[it->warp].name : it->label;
    const char *ibDesc  = (it->warp >= 0) ? warps[it->warp].desc : it->desc;
    if (!ibDesc) return;

    int bw = 264, bx = WIN_X + (WIN_W - bw) / 2;
    char lines[8][64];
    int nlines = 0;

    const char *s = T(ibDesc);
    // Live hotkey: features with a rebindable button carry a {HK} token in their desc; swap it for
    // the currently-mapped glyph so the card always shows the real button (updates on next open).
    char hkBuf[256];
    if (it->cheat == CH_MOONJUMP || it->cheat == CH_FASTMOVE)
    {
        const char *gl = hotKeys[it->cheat == CH_MOONJUMP ? mjKey : fmKey].glyph; // e.g. "{Y}"
        int o = 0;
        for (int i = 0; s[i] && o < (int)sizeof(hkBuf) - 8; )
        {
            if (s[i] == '{' && s[i+1] == 'H' && s[i+2] == 'K' && s[i+3] == '}')
            { for (int k = 0; gl[k] && o < (int)sizeof(hkBuf) - 1; ++k) hkBuf[o++] = gl[k]; i += 4; }
            else hkBuf[o++] = s[i++];
        }
        hkBuf[o] = 0;
        s = hkBuf;
    }
    while (*s && nlines < 8)
    {
        int len = 0;
        lines[nlines][0] = 0;
        while (*s)
        {
            while (*s == ' ') s++;
            if (*s == '\n') { s++; break; } // hard line break
            if (!*s) break;
            int wl = 0;
            while (s[wl] && s[wl] != ' ' && s[wl] != '\n') wl++;

            char tmp[64];
            int tl = len;
            memcpy(tmp, lines[nlines], (size_t)len);
            if (tl && tl < 62) tmp[tl++] = ' ';
            for (int k = 0; k < wl && tl < 63; ++k) tmp[tl++] = s[k];
            tmp[tl] = 0;

            if (len && CTextBtnWidth(tmp) > bw - 24) break; // word starts the next line (glyph-aware)
            memcpy(lines[nlines], tmp, (size_t)tl + 1);
            len = tl;
            s += wl;
        }
        nlines++;
    }

    int bh = 34 + nlines * 16 + 10;
    int by = WIN_Y + (WIN_H - bh) / 2;

    // The InfoBox is a fixed dark tooltip; use theme-independent light gold/ink so it stays
    // readable over EVERY theme (light themes' own INK/GOLD are dark and would vanish here).
    #define IB_GOLD 236, 200, 120
    #define IB_INK  248, 240, 216
    CFill(bx, by, bw, bh, 24, 18, 10);
    CFill(bx, by, bw, 1, IB_GOLD); CFill(bx, by + bh - 1, bw, 1, IB_GOLD);
    CFill(bx, by, 1, bh, IB_GOLD); CFill(bx + bw - 1, by, 1, bh, IB_GOLD);
    // title without any " (...)" tail - the description below already explains it
    char tit[48]; { const char *L = T(ibLabel); int k = 0;
        while (L[k] && k < 47 && !(L[k] == ' ' && L[k + 1] == '(')) { tit[k] = L[k]; k++; } tit[k] = 0; }
    CTextBtn(bx + 12, by + 6, tit, IB_GOLD, 1);
    CFill(bx + 12, by + 23, CTextBtnWidth(tit) + 6, 1, IB_GOLD);
    for (int i = 0; i < nlines; ++i)
        CTextBtn(bx + 12, by + 30 + i * 16, lines[i], IB_INK, 0);
    #undef IB_GOLD
    #undef IB_INK

    Present(); Present();

    // wait for any fresh key press, then release
    u32 prev = HID_PAD;
    while (1)
    {
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD;
        if (pad & ~prev) { if (pad & BUTTON_SELECT) g_quitToGame = 1; break; }
        prev = pad;
    }
    DrainButtons(~0u); // capped: a stuck pad must not hang the console with the game paused
}

// Picker list UI (bottle contents, inventory item). Returns after A (write) or B (cancel).
static void PickerRun(const Picker *pk)
{
    int cursor = 0, scroll = 0, changed = 1;
    u8 cur = R8(pk->addr);
    for (int k = 0; k < pk->count; ++k)
        if (pk->opts[k].val == cur) { cursor = k; break; }

    u32 prev = HID_PAD;

    while (1)
    {
        if (changed)
        {
            if (cursor < scroll)             scroll = cursor;
            if (cursor >= scroll + MAX_ROWS) scroll = cursor - MAX_ROWS + 1;

            ComposeBackdrop();
            int tw = CTextWidth(T(pk->title));
            CText(WIN_X + 12, WIN_Y + 7, T(pk->title), GOLD, 1);
            CFill(WIN_X + 12, WIN_Y + 24, tw + 6, 1, GOLD);

            int listW = ROW_W - 74; // leave room for the big preview on the right
            for (int i = scroll; i < pk->count && i < scroll + MAX_ROWS; ++i)
            {
                int y = ROW_Y0 + (i - scroll) * ROW_H;
                if (i == cursor)
                {
                    CFillBlend(ROW_X - 4, y - 1, listW + 8, ROW_H, 0, 0, 0, 110);
                    CFill(ROW_X - 4, y - 1, 2, ROW_H, GOLD);
                }
                DrawSprite(ROW_X, y - 1, pk->opts[i].val, 0);
                const u8 *oc = (pk->opts[i].val == cur) ? CGREEN : CINK;
                CTextClip(ROW_X + 20, y - 1, T(pk->opts[i].name), listW - 20, oc[0], oc[1], oc[2], 0);
            }

            // big preview of the highlighted item (42px original icon)
            {
                int px = WIN_X + WIN_W - 68, py = ROW_Y0 + 14;
                CFillBlend(px - 6, py - 6, 54, 54, 0, 0, 0, 90);
                CFill(px - 6, py - 6, 54, 1, GOLD); CFill(px - 6, py + 47, 54, 1, GOLD);
                CFill(px - 6, py - 6, 1, 54, GOLD); CFill(px + 47, py - 6, 1, 54, GOLD);
                DrawSprite(px, py, pk->opts[cursor].val, 1);
            }

            if (scroll > 0)
                for (int i = 0; i < 4; ++i) CFill(WIN_X + WIN_W - 14 - i, ROW_Y0 + 3 + i, 1 + 2 * i, 1, GOLD);
            if (scroll + MAX_ROWS < pk->count)
                for (int i = 0; i < 4; ++i) CFill(WIN_X + WIN_W - 14 - i, ROW_Y0 + MAX_ROWS * ROW_H - 4 - i, 1 + 2 * i, 1, GOLD);

            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 16, T("{A} set   {B} cancel"), INK_DIM);
            Present(); Present();
            changed = 0;
        }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if (down & BUTTON_DOWN) { cursor = (cursor + 1 < pk->count) ? cursor + 1 : 0; changed = 1; }
        if (down & BUTTON_UP)   { cursor = (cursor > 0) ? cursor - 1 : pk->count - 1; changed = 1; }
        if (down & (BUTTON_RIGHT | BUTTON_R1)) { cursor += MAX_ROWS; if (cursor >= pk->count) cursor = pk->count - 1; changed = 1; } // D-Pad L/R page
        if (down & (BUTTON_LEFT | BUTTON_L1))  { cursor -= MAX_ROWS; if (cursor < 0) cursor = 0; changed = 1; }
        if (down & BUTTON_A)
        {
            W8(pk->addr, pk->opts[cursor].val);
            QueueToastRaw(T(pk->opts[cursor].name), T(": SET"));
            break;
        }
        if (down & BUTTON_SELECT) g_quitToGame = 1;
        if (down & (BUTTON_B | BUTTON_SELECT)) break;
    }
}
