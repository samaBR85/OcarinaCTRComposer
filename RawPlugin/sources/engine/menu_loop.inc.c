// ===================== Game pause (Luma thread scheduler) =====================
#define THREADVARS_MAGIC  0x21545624
static bool ThreadPredicate(void *thread_)
{
    u32   tls     = *(volatile u32 *)((u8 *)thread_ + 0x94);
    void *current = *(void **)0xFFFF9000;
    if (current != thread_ && *(volatile u32 *)tls != THREADVARS_MAGIC) return true;
    return false;
}
static void PauseGame(void)  { svcControlProcess(CUR_PROCESS_HANDLE, PROCESSOP_SCHEDULE_THREADS, 1, (u32)ThreadPredicate); }
static void ResumeGame(void) { svcControlProcess(CUR_PROCESS_HANDLE, PROCESSOP_SCHEDULE_THREADS, 0, (u32)ThreadPredicate); }

// ===================== Menu loop =====================
// Navigation state is persistent: reopening the menu returns to the last spot.
static int fstack[8], cstack[8], sstack[8], menuDepth = 0;
static int menuFolder = F_ROOT, menuCursor = 1, menuScroll = 0; // cursor 1 = first HOME item (0 is a separator)

// Is a theme's background light? (same luminance heuristic as ThemeBgLight, for any theme)
static int ThemeIdxLight(int i)
{ const Theme *t = &THEMES[i]; return (t->bg[0]*30 + t->bg[1]*59 + t->bg[2]*11)/100 > 140; }

// Build the list of theme indices matching a filter (0=all, 1=light, 2=dark). Returns the count.
static int ThemeFilterBuild(int filt, int *flt)
{
    int fn = 0;
    for (int i = 0; i < THEME_COUNT; ++i)
    {
        int light = ThemeIdxLight(i);
        if (filt == 1 && !light) continue;
        if (filt == 2 && light) continue;
        flt[fn++] = i;
    }
    return fn;
}

// Live theme picker (Settings -> Change Theme). Moving the cursor previews the
// theme instantly (the whole list recolors). A keeps it (saved on menu close),
// B reverts to the theme active on entry, SELECT keeps it and jumps to the game.
// Y cycles a light/dark filter so long lists are quicker to sort through.
static void ThemePicker(void)
{
    static int themeFilt = 0;           // 0=all 1=light 2=dark, persists across opens
    int startIdx = g_themeIdx;
    int flt[THEME_COUNT];
    int fn = ThemeFilterBuild(themeFilt, flt);
    if (fn == 0) { themeFilt = 0; fn = ThemeFilterBuild(0, flt); }
    int sel = 0;                         // index INTO flt[]
    for (int i = 0; i < fn; ++i) if (flt[i] == g_themeIdx) { sel = i; break; }
    int scroll = 0, redraw = 1;
    u32 prev = HID_PAD;
    while (1)
    {
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + MAX_ROWS) scroll = sel - MAX_ROWS + 1;
        if (redraw)
        {
            ApplyTheme(flt[sel]); // live preview: recolors everything drawn below
            ComposeBackdrop();
            CText(WIN_X + 12, WIN_Y + 7, T("Change Theme"), INK, 1);
            CFill(WIN_X + 12, WIN_Y + 24, CTextWidth("Change Theme") + 6, 1, GOLD);
            char pos[16]; siprintf(pos, "%d/%d", sel + 1, fn);
            CText6(WIN_X + WIN_W - 12 - C6Width(pos), WIN_Y + 9, pos, INK_DIM);
            for (int r = 0; r < MAX_ROWS; ++r)
            {
                int fi = scroll + r; if (fi >= fn) break;
                int i = flt[fi];
                int y = ROW_Y0 + r * ROW_H;
                if (fi == sel)
                {
                    CFillBlend(ROW_X - 4, y - 1, ROW_W + 8, ROW_H, 0, 0, 0, 110);
                    CFill(ROW_X - 4, y - 1, 2, ROW_H, GOLD);
                }
                const Theme *t = &THEMES[i];
                int sx = ROW_X + 2; // 4 color swatches: bg / gold / text / on
                CFill(sx,      y + 3, 8, 9, t->bg[0], t->bg[1], t->bg[2]);
                CFill(sx + 9,  y + 3, 8, 9, t->gold[0], t->gold[1], t->gold[2]);
                CFill(sx + 18, y + 3, 8, 9, t->ink[0], t->ink[1], t->ink[2]);
                CFill(sx + 27, y + 3, 8, 9, t->green[0], t->green[1], t->green[2]);
                CText(sx + 40, y - 1, t->name, RGB3(fi == sel ? CGREEN : CINK), 0);
            }
            if (scroll > 0)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, ROW_Y0 + 3 + a, 1 + 2 * a, 1, GOLD);
            if (scroll + MAX_ROWS < fn)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, ROW_Y0 + MAX_ROWS * ROW_H - 4 - a, 1 + 2 * a, 1, GOLD);
            const char *fmode = themeFilt == 1 ? T("{Y} light") : themeFilt == 2 ? T("{Y} dark") : T("{Y} all");
            char leg[96]; sniprintf(leg, sizeof leg, "%s  %s  %s  %s", T("{A} apply"), T("{B} cancel"), T("{L}/{R} page"), fmode);
            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 16, leg, INK_DIM);
            Present(); Present();
            ComposeBottom(); BotBlitComposeBoth(); // live-recolor the bottom screen with the previewed theme
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if (down & BUTTON_DOWN) { sel = (sel + 1 < fn) ? sel + 1 : 0; redraw = 1; }
        if (down & BUTTON_UP)   { sel = (sel > 0) ? sel - 1 : fn - 1; redraw = 1; }
        if (down & (BUTTON_RIGHT | BUTTON_R1)) { sel += MAX_ROWS; if (sel >= fn) sel = fn - 1; redraw = 1; }
        if (down & (BUTTON_LEFT | BUTTON_L1))  { sel -= MAX_ROWS; if (sel < 0) sel = 0; redraw = 1; }
        if (down & BUTTON_Y)    { int cur = flt[sel]; // keep the current theme selected across the filter change
                                  themeFilt = (themeFilt + 1) % 3;
                                  fn = ThemeFilterBuild(themeFilt, flt);
                                  if (fn == 0) { themeFilt = 0; fn = ThemeFilterBuild(0, flt); }
                                  sel = 0; for (int i = 0; i < fn; ++i) if (flt[i] == cur) { sel = i; break; }
                                  scroll = 0; redraw = 1; }
        if (down & BUTTON_A)      { ApplyTheme(flt[sel]); configDirty = 1; QueueToastRaw("Theme:", THEMES[flt[sel]].name); return; }
        if (down & BUTTON_B)      { ApplyTheme(startIdx); ComposeBottom(); BotBlitComposeBoth(); return; } // revert the previewed bottom too
        if (down & BUTTON_SELECT) { ApplyTheme(flt[sel]); configDirty = 1; g_quitToGame = 1; return; }
    }
}

// First-launch language chooser. Live-previews each language as you scroll;
// any of A/B/SELECT confirms (English stays the default if untouched).
static void LanguagePicker(void)
{
    int sel = g_langIdx;
    u32 prev = HID_PAD;
    int redraw = 1;
    LangProbeAvail(); // refresh which languages have SD files, so missing ones show red
    while (1)
    {
        if (redraw)
        {
            g_langIdx = sel; LangLoad(); // live preview
            ComposeBackdrop();
            CText(WIN_X + 12, WIN_Y + 7, T("Select Language"), INK, 1);
            CFill(WIN_X + 12, WIN_Y + 24, CTextWidth(T("Select Language")) + 6, 1, GOLD);
            for (int i = 0; i < NUM_LANGS; ++i)
            {
                int y = ROW_Y0 + i * ROW_H;
                if (i == sel)
                {
                    CFillBlend(ROW_X - 4, y - 1, ROW_W + 8, ROW_H, 0, 0, 0, 110);
                    CFill(ROW_X - 4, y - 1, 2, ROW_H, GOLD);
                }
                if (!g_langAvail[i])   CText(ROW_X + 10, y - 1, kLangLabels[i], 225, 60, 45, 0);   // no SD file -> red
                else if (i == sel)     CText(ROW_X + 10, y - 1, kLangLabels[i], GREEN_ON, 0);
                else                   CText(ROW_X + 10, y - 1, kLangLabels[i], INK, 0);
            }
            CText6(WIN_X + 12, WIN_Y + WIN_H - 16, T("A: confirm"), INK_DIM);
            Present(); Present();
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if (down & BUTTON_DOWN) { sel = (sel + 1 < NUM_LANGS) ? sel + 1 : 0; redraw = 1; }
        if (down & BUTTON_UP)   { sel = (sel > 0) ? sel - 1 : NUM_LANGS - 1; redraw = 1; }
        if (down & (BUTTON_A | BUTTON_SELECT)) // any A/SELECT confirms; there's no cancel on first run
        {
            g_langIdx = sel; LangLoad(); GuideLoad(); configDirty = 1; // save so it won't ask again
            return;
        }
    }
}

// Flip the age that applies on the next load. In an overworld scene that has BOTH age setups it
// also reloads NOW for an instant change; in age-locked scenes (interiors/dungeons) an immediate
// reload would crash, so there we just set the flag and it applies on the next door/Teleport.
// Returns 1 if it triggered a reload (caller must resume the game), 0 otherwise.
static int ToggleAge(void)
{
    if (!LinkPtr()) { QueueToastRaw("Toggle Age: not in game", ""); return 0; }
    u8 na = R8(GCTX_BASE + GCTX_LINKAGE) ? 0 : 1;   // linkAgeOnLoad: 0=Adult, 1=Child
    W8(GCTX_BASE + GCTX_LINKAGE, na);
    // Overworld scenes with both child & adult setups (safe to reload in place). Everything else
    // (Link's House, dungeons, desert, castle) has only one age's setup -> flag-flip fallback.
    static const u16 dualAge[] = { 0x51,0x52,0x53,0x54,0x55,0x57,0x58,0x5B,0x60,0x61,0x62,0x63 };
    u16 sc = R16(GCTX_BASE + GCTX_SCENENUM);
    for (unsigned i = 0; i < sizeof(dualAge)/sizeof(dualAge[0]); ++i)
        if (sc == dualAge[i]) { if (DoWarp(0xFFFF, na)) { QueueToastRaw(na ? "Now: Child" : "Now: Adult", ""); return 1; } break; }
    QueueToastRaw(na ? "Child on next area" : "Adult on next area", "");
    return 0;
}

// Warp to the current waypoint slot via the game's void-out/respawn: write the saved pos + room +
// entrance into gSaveContext.respawn[0], flag a respawn, then reload the scene. Unlike a raw coord
// write this loads the correct room + collision, so it's reliable in villages/dungeons too.
// Returns 1 if it started the warp (caller resumes the game), 0 on failure.
static int WarpToWaypoint(void)
{
    int s = g_wpSlot;
    if (!g_wp[s].valid) { QueueToastRaw("Slot empty - Save first", ""); return 0; }
    if (!GctxValid())   { QueueToastRaw("Can't warp right now", "");    return 0; }
    u32 r = 0x0588E48; // gSaveContext.respawn[0]  (RESPAWN_MODE_DOWN)
    W32(r + 0x00, g_wp[s].x); W32(r + 0x04, g_wp[s].y); W32(r + 0x08, g_wp[s].z);
    W16(r + 0x0C, 0);              // yaw (facing not saved - Link faces the default direction)
    W16(r + 0x0E, 0x0DFF);        // playerParams: a normal standing spawn
    W16(r + 0x10, g_wp[s].entrance);
    W8 (r + 0x12, g_wp[s].room);
    W8 (r + 0x13, 0);
    W32(r + 0x14, R32(0x08720C44)); // keep the current switch flags (actorCtx.flags.tempSwch)
    W32(r + 0x18, R32(0x08720C60)); // and collectible flags (tempCollect)
    W8 (0x0588E44, 1);            // respawnFlag = 1 -> spawn at respawn[0], not the entrance's point
    DoWarp(g_wp[s].entrance, -1);  // reload the scene (respawnFlag makes it use our pos+room)
    return 1;
}

// Cycle a Settings "picker" cheat by dir (+1 next / -1 previous). Shared by the A button (dir=+1) and
// D-pad ←/→, so both stay in sync. Applies the same side effects/toasts as the old A-only handlers.
// Returns 1 if it handled `cheat`, 0 otherwise (e.g. Theme, which opens a full picker instead).
static int CfgCycle(int cheat, int dir)
{
    int step = (dir > 0) ? 1 : -1;
    switch (cheat)
    {
        case CH_CFG_QMKEY:
            qmCombo = (qmCombo + step + NUM_QMCOMBOS) % NUM_QMCOMBOS;
            QueueToastRaw(qmCombos[qmCombo].plain, ": SET"); configDirty = 1; return 1;
        case CH_CFG_MJKEY:
            mjKey = (mjKey + step + NUM_HOTKEYS) % NUM_HOTKEYS;
            QueueToastRaw("MoonJump: ", hotKeys[mjKey].glyph); configDirty = 1; return 1;
        case CH_CFG_FMKEY:
            fmKey = (fmKey + step + NUM_HOTKEYS) % NUM_HOTKEYS;
            QueueToastRaw("Fast Move: ", hotKeys[fmKey].glyph); configDirty = 1; return 1;
        case CH_CFG_LANG:
            g_langIdx = (g_langIdx + step + NUM_LANGS) % NUM_LANGS;
            LangLoad(); GuideLoad(); QueueToastRaw(kLangLabels[g_langIdx], "");
            ComposeBottom(); BotBlitComposeBoth(); // relocalize the bottom legend now
            configDirty = 1; return 1;
    }
    return 0;
}

static void RunMenu(void)
{
    int depth = menuDepth;
    int folderIdx = menuFolder, cursor = menuCursor, scroll = menuScroll;

    if (!gCompose) return;
    SysFontInit();
    g_quitToGame = 0; // fresh; a sub-loop sets this to request "exit to game"

    PauseGame();
    TopTakeOver(); // remember which buffer the game was showing, so we can hand it back

    BotGrab();
    ComposeBottom();
    BotBlitComposeBoth();

    if (g_qmHandoff)
    {
        g_qmHandoff = 0;
        RestoreTopBackdrop(); // savedTop = the real game frame, saved before the quick menu painted
    }
    else
    {
        GrabFb();
    }
    DimOutsideWindow();
    CaptureTopBackdrop(); // save clean backdrop so top redraws stay bleed-free

    // Draw the top-screen menu NOW, before waiting for SELECT to be released - otherwise the top
    // stays blank (game backdrop only) until the user lets go of SELECT, while the bottom is
    // already up. Skip it for the first-run language picker / tool-resume paths, which paint their
    // own screens right after the wait (an early menu frame would just flash before them).
    if (!g_firstRun && g_resumeTool < 0)
    {
        ComposeMenu(&folders[folderIdx], depth, cursor, scroll);
        Present(); Present();
    }

    DrainButtons(BUTTON_SELECT); // capped: a stuck SELECT must not hang the console
    u32 prev = HID_PAD;

    // First ever launch (no Settings.cfg): ask for a language before anything else.
    if (g_firstRun && fsReady)
    {
        g_firstRun = 0;
        LanguagePicker();
        ComposeBottom(); BotBlitComposeBoth(); // bottom legend in the chosen language
        prev = HID_PAD;
    }

    // Coming back from an exit-to-game that happened inside a tool: drop the
    // player straight back into that tool instead of the Tools folder.
    if (g_resumeTool >= 0)
    {
        int t = g_resumeTool; g_resumeTool = -1;
        ToolRun(t);
        if (g_quitToGame) g_resumeTool = t;                  // SELECT again -> keep resuming
        else { ComposeBottom(); BotBlitComposeBoth(); prev = HID_PAD; }
    }

    if (!g_quitToGame)
    {
    ComposeMenu(&folders[folderIdx], depth, cursor, scroll);
    Present();
    Present();

    while (1)
    {
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        const Folder *fld = &folders[folderIdx];
        int changed = 0;

        if (folderIdx == F_ROOT || folderIdx == F_TELEPORT) // grouped 2-column grid
        {
            BuildRootLayout(fld);
            if (NavSkip(folderIdx, cursor)) cursor = RootFirstSel(fld);
            if (down & BUTTON_UP)    { cursor = RootNeighbor(fld, cursor, 0); changed = 1; }
            if (down & BUTTON_DOWN)  { cursor = RootNeighbor(fld, cursor, 1); changed = 1; }
            if (down & BUTTON_LEFT)  { cursor = RootNeighbor(fld, cursor, 2); changed = 1; }
            if (down & BUTTON_RIGHT) { cursor = RootNeighbor(fld, cursor, 3); changed = 1; }
        }
        else
        {
            // never rest on a non-selectable row (section header OR a filtered-out Teleport item)
            if (NavSkip(folderIdx, cursor)) { int c = cursor; do { c = (c + 1 < fld->count) ? c + 1 : 0; } while (NavSkip(folderIdx, c) && c != cursor); if (c != cursor) changed = 1; cursor = c; }
            // wrap-around navigation, skipping headers and hidden rows
            if (down & BUTTON_DOWN) { int c = cursor; do { c = (c + 1 < fld->count) ? c + 1 : 0; } while (NavSkip(folderIdx, c) && c != cursor); cursor = c; changed = 1; }
            if (down & BUTTON_UP)   { int c = cursor; do { c = (c > 0) ? c - 1 : fld->count - 1; } while (NavSkip(folderIdx, c) && c != cursor); cursor = c; changed = 1; }
            // On a "picker" row (Waypoint Slot, Language, the hotkey cyclers), D-pad ←/→ change the value
            // (the shoulders L/R still page). Everywhere else D-pad ←/→ and the shoulders all jump a page,
            // clamped to the ends, skipping headers/hidden rows.
            int rowCheat = fld->items[cursor].cheat;
            int isCycler = (rowCheat == CH_WP_SLOT || rowCheat == CH_CFG_LANG ||
                            rowCheat == CH_CFG_QMKEY || rowCheat == CH_CFG_MJKEY || rowCheat == CH_CFG_FMKEY);
            if (isCycler && (down & (BUTTON_LEFT | BUTTON_RIGHT)))
            {
                int dir = (down & BUTTON_RIGHT) ? 1 : -1;
                if (rowCheat == CH_WP_SLOT)
                {
                    g_wpSlot = (g_wpSlot + (dir > 0 ? 1 : 8)) % 9; g_wpDirty = 1;
                    WpName(g_wpSlot, g_wpMsg, sizeof g_wpMsg); QueueToastRaw(g_wpMsg, "");
                }
                else CfgCycle(rowCheat, dir);
                changed = 1;
            }
            else
            {
            if (down & (BUTTON_RIGHT | BUTTON_R1)) { int c = cursor; for (int k = 0; k < MAX_ROWS && c + 1 < fld->count; ++k) c++;
                                      while (c < fld->count && NavSkip(folderIdx, c)) c++;
                                      if (c >= fld->count) c = cursor;
                                      if (c != cursor) { cursor = c; changed = 1; } }
            if (down & (BUTTON_LEFT | BUTTON_L1))  { int c = cursor; for (int k = 0; k < MAX_ROWS && c > 0; ++k) c--;
                                      while (c > 0 && NavSkip(folderIdx, c)) c--;
                                      if (c != cursor) { cursor = c; changed = 1; } }
            }
        }

        if (down & BUTTON_A)
        {
            const Item *it = &fld->items[cursor];
            if (it->folder >= 0)
            {
                fstack[depth] = folderIdx; cstack[depth] = cursor; sstack[depth] = scroll; depth++;
                folderIdx = it->folder; cursor = 0; scroll = 0;
                // land on the first selectable row so the very first frame shows the highlight
                // (a folder like Settings opens on a section-header separator otherwise)
                { const Folder *nf = &folders[folderIdx];
                  while (cursor < nf->count && NavSkip(folderIdx, cursor)) cursor++;
                  if (cursor >= nf->count) cursor = 0; }
                changed = 1;
            }
            else if (it->warp == -2) // Teleport filter row: cycle All -> Overworld -> Dungeons
            { g_tpFilter = (g_tpFilter + 1) % 3; scroll = 0; changed = 1; }
            else if (it->warp >= 0) // teleport: write the entrance, then resume so the game loads it
            {
                if (DoWarp(warps[it->warp].entrance, warps[it->warp].age))
                {
                    QueueToastRaw(T(warps[it->warp].name), T(": WARP"));
                    g_quitToGame = 1; // hand control back so the scene transition runs
                    break;
                }
                QueueToastRaw("Can't warp right now", ""); // not in game, or unsafe (base check failed)
                changed = 1;
            }
            else if (it->picker >= 0)
            {
                PickerRun(&pickers[it->picker]);
                if (g_quitToGame) break; // SELECT inside picker -> straight to game
                prev = HID_PAD;
                changed = 1;
            }
            else if (it->tool >= 0)
            {
                ToolRun(it->tool);
                if (g_quitToGame) { g_resumeTool = it->tool; break; } // re-enter this tool on reopen
                ComposeBottom(); BotBlitComposeBoth(); // tool owned the bottom; restore menu legend
                prev = HID_PAD;
                changed = 1;
            }
            else if (it->cheat == CH_CFG_QMKEY || it->cheat == CH_CFG_MJKEY || it->cheat == CH_CFG_FMKEY)
            {
                CfgCycle(it->cheat, 1); // A advances; ←/→ also cycle (shared with CfgCycle)
                changed = 1;
            }
            else if (it->cheat == CH_CFG_HKRESET)
            {
                qmCombo = 0; mjKey = 2; fmKey = 1; // defaults: L+SELECT / Y / X
                QueueToastRaw("Hotkeys reset to default", "");
                configDirty = 1;
                changed = 1;
            }
            else if (it->cheat == CH_CFG_THEME)
            {
                ThemePicker();
                if (g_quitToGame) break; // SELECT inside picker -> straight to game
                prev = HID_PAD;
                changed = 1;
            }
            else if (it->cheat == CH_CFG_LANG)
            {
                CfgCycle(CH_CFG_LANG, 1); // A advances; ←/→ also cycle (shared with CfgCycle)
                changed = 1;
            }
            else if (it->cheat == CH_TOGGLE_AGE) // flip age; instant reload in overworld, else next-load
            {
                if (ToggleAge()) { g_quitToGame = 1; break; } // reloaded -> hand control back
                flashCheat = CH_TOGGLE_AGE; flashTicks = 40; // brief self-disabling blink (flag-flip case)
                changed = 1;
            }
            else if (it->cheat == CH_WP_SLOT) // cycle which of the 9 waypoint slots Save/Warp use
            {
                g_wpSlot = (g_wpSlot + 1) % 9; g_wpDirty = 1;
                WpName(g_wpSlot, g_wpMsg, sizeof g_wpMsg); QueueToastRaw(g_wpMsg, "");
                changed = 1;
            }
            else if (it->cheat == CH_WP_WARP) // reliable warp: reload the scene at the saved spot
            {
                if (WarpToWaypoint()) { g_quitToGame = 1; break; }
                changed = 1;
            }
            else if (OneShot(it->cheat))
            {
                char sfx[48]; sniprintf(sfx, sizeof sfx, ": %s", g_oneShotMsg);
                QueueToastRaw(T(it->label), sfx);
                flashMsg = g_oneShotMsg;
                flashCheat = it->cheat; flashTicks = 50; // ~0.8s feedback
                changed = 1;
            }
            else
            {
                cheatState[it->cheat] ^= 1;
                if (it->cheat == CH_CFG_TOAST || it->cheat == CH_CFG_AUTOFILL) configDirty = 1; // settings toggles: persist, no self-toast
                else QueueToast(T(it->label), cheatState[it->cheat]);
                changed = 1;
            }
        }

        if (down & BUTTON_Y)
        {
            const Item *it = &fld->items[cursor];
            if (it->warp >= 0) // star a teleport destination for the quick menu
            { warpFav[it->warp] ^= 1; favDirty = 1; changed = 1; }
            else if (it->folder >= 0) // star a folder -> quick menu shortcut that opens it
            { folderFav[it->folder] ^= 1; favDirty = 1; changed = 1; }
            else if (it->tool >= 0) // star a tool -> quick menu shortcut that launches it
            { toolFav[it->tool] ^= 1; favDirty = 1; changed = 1; }
            else if (it->folder < 0 && it->picker < 0 && it->tool < 0 &&
                it->cheat >= 0 && it->cheat != CH_CFG_QMKEY && it->cheat != CH_CFG_THEME &&
                it->cheat != CH_CFG_LANG && it->cheat != CH_CFG_MJKEY && it->cheat != CH_CFG_FMKEY &&
                it->cheat != CH_CFG_HKRESET && it->cheat != CH_CFG_TOAST && it->cheat != CH_CFG_AUTOFILL)
            { favorite[it->cheat] ^= 1; favDirty = 1; changed = 1; }
        }

        if (down & BUTTON_START) // on a Waypoint row: reset all 9 saved waypoints
        {
            const Item *it = &fld->items[cursor];
            if (it->cheat == CH_WP_SLOT || it->cheat == CH_WP_SAVE || it->cheat == CH_WP_WARP)
            { memset(g_wp, 0, sizeof(g_wp)); g_wpDirty = 1; QueueToastRaw("All waypoints reset", ""); changed = 1; }
        }

        if (down & BUTTON_X)
        {
            const Item *it = &fld->items[cursor];
            if (it->desc)
            {
                InfoBox(it);
                if (g_quitToGame) break; // SELECT dismissed the info box -> to game
                prev = HID_PAD;
                changed = 1;
            }
        }

        if (down & BUTTON_B)
        {
            if (depth > 0) { depth--; folderIdx = fstack[depth]; cursor = cstack[depth]; scroll = sstack[depth]; changed = 1; }
            else break;
        }
        if (down & BUTTON_SELECT) break;

        if (flashTicks > 0 && --flashTicks == 0) { flashCheat = -1; changed = 1; }

        if (folderIdx == F_ROOT) scroll = 0; // HOME fits in the grid, never scrolls
        else if (folderIdx == F_TELEPORT) // 2-col grid: scroll is a pixel offset that follows the cursor
        {
            BuildRootLayout(fld);
            int visBot = WIN_Y + WIN_H - 22, cy = g_rlY[cursor];
            if (cy - scroll < ROW_Y0)         scroll = cy - ROW_Y0;
            if (cy + ROW_H - scroll > visBot) scroll = cy + ROW_H - visBot;
            if (scroll < 0) scroll = 0;
        }
        else
        {
            int cvp = VisPos(folderIdx, cursor); // scroll tracks the cursor's VISIBLE position
            if (cvp < scroll)             scroll = cvp;
            if (cvp >= scroll + MAX_ROWS) scroll = cvp - MAX_ROWS + 1;
        }

        if (changed) { ComposeMenu(&folders[folderIdx], depth, cursor, scroll); Present(); }
    }
    } // end if (!g_quitToGame)

    flashCheat = -1; flashTicks = 0;
    menuDepth = depth; menuFolder = folderIdx; menuCursor = cursor; menuScroll = scroll;

    BotRestoreBoth();
    TopRelease(); // hand the top screen back too, else it stays frozen on our last frame
    DrainButtons(BUTTON_B | BUTTON_SELECT | BUTTON_A); // let go of B before the game sees it (else: sword swing)
    ResumeGame();

    if (configDirty) { ConfigSave(); configDirty = 0; } // write after resuming (fs is slow)
    if (favDirty)    { FavSave();    favDirty = 0; }
    if (g_wpDirty)   { WpSave();     g_wpDirty = 0; }
}
