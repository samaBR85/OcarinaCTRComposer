// ===================== Quick menu (favorites, L+SELECT) =====================
static void QuickMenu(void)
{
    struct { const char *label; int cheat; const Item *it; char sl[40]; } ent[12];
    int n = 0;
    for (int f = 0; f < NUM_FOLDERS; ++f)
        for (int i = 0; i < folders[f].count && n < 12; ++i)
        {
            const Item *it = &folders[f].items[i];
            if (it->cheat >= 0 && it->folder < 0 && it->picker < 0 && it->tool < 0 && favorite[it->cheat])
            {
                ent[n].label = it->label; ent[n].cheat = it->cheat; ent[n].it = it;
                // short label: drop any " (...)" tail so the panel stays narrow (X shows the full info)
                const char *L = T(it->label); int k = 0;
                while (L[k] && k < 39 && !(L[k] == ' ' && L[k + 1] == '(')) { ent[n].sl[k] = L[k]; k++; }
                ent[n].sl[k] = 0;
                n++;
            }
            else if (it->warp >= 0 && warpFav[it->warp]) // starred teleport destination
            {
                ent[n].label = warps[it->warp].name; ent[n].cheat = -1; ent[n].it = it;
                const char *L = T(warps[it->warp].name); int k = 0;
                while (L[k] && k < 39) { ent[n].sl[k] = L[k]; k++; }
                ent[n].sl[k] = 0;
                n++;
            }
            else if (it->folder >= 0 && folderFav[it->folder]) // starred folder shortcut
            {
                ent[n].label = it->label; ent[n].cheat = -1; ent[n].it = it;
                const char *L = T(it->label); int k = 0;
                while (L[k] && k < 39) { ent[n].sl[k] = L[k]; k++; }
                ent[n].sl[k] = 0;
                n++;
            }
            else if (it->tool >= 0 && toolFav[it->tool]) // starred tool shortcut
            {
                ent[n].label = it->label; ent[n].cheat = -1; ent[n].it = it;
                const char *L = T(it->label); int k = 0;
                while (L[k] && k < 39) { ent[n].sl[k] = L[k]; k++; }
                ent[n].sl[k] = 0;
                n++;
            }
        }

    if (!gCompose) return;
    SysFontInit();   // idempotent - ensures the system font is loaded even when the quick menu is
                     // the FIRST thing opened after boot (else info boxes fall back to the tiny font)
    PauseGame();
    TopTakeOver();
    GrabFb();
    CaptureTopBackdrop(); // save the game frame so we can repaint cleanly (e.g. after the X info box)

    // Compact panel: small 6x10 font, short rows. Icons are the full 16px DrawCheatIcon (same as
    // the main menu) so the hand-drawn vector icons (moon, pin, portal, ...) show here too - the
    // old path only blitted real sprites via FindSprite, so vector-icon cheats had a blank slot.
    // Column layout: checkbox @+6, icon @+18 (16px), label @+38 (relative to panel x)
    #define QM_X   8
    #define QM_Y   8
    #define QM_RH  17
    #define QM_LBL 38
    int rows = n ? n : 1;
    int w = QM_LBL + C6Width("Favorites") + 6;
    for (int i = 0; i < n; ++i)
    {
        int lw = QM_LBL + C6Width(ent[i].sl) + 6;
        if (lw > w) w = lw;
    }
    if (!n)
    {
        int lw = 7 + C6Width("Press Y in the menu to star") + 6;
        if (lw > w) w = lw;
    }
    if (w > 384 - 16) w = 384 - 16;
    int h = 20 + rows * QM_RH + 6;

    DrainButtons(BUTTON_SELECT); // capped: a stuck SELECT must not hang the console
    u32 prev = HID_PAD;
    static int qmLastCursor = 0;              // reopen on the entry you last had selected
    int cursor = (qmLastCursor < n) ? qmLastCursor : (n > 0 ? n - 1 : 0);
    int changed = 1;

    while (1)
    {
        if (changed)
        {
            // auto-contrast: light theme bg -> dark text, dark bg -> light text (keeps every theme readable)
            int bgLight = ThemeBgLight();
            u8 tR = bgLight ? 28 : 238, tG = bgLight ? 26 : 236, tB = bgLight ? 30 : 224;   // off label
            u8 gR = bgLight ? 22 : 150, gG = bgLight ? 108 : 236, gB = bgLight ? 30 : 130;  // on label (green)
            u8 aR = bgLight ? 150 : CGOLD[0], aG = bgLight ? 100 : CGOLD[1], aB = bgLight ? 10 : CGOLD[2]; // gold accent
            RestoreTopBackdrop(); // repaint the game frame (clears any prior info box / stale box)
            CFill(QM_X, QM_Y, w, h, BG);
            CFill(QM_X, QM_Y, w, 1, aR, aG, aB); CFill(QM_X, QM_Y + h - 1, w, 1, aR, aG, aB);
            CFill(QM_X, QM_Y, 1, h, aR, aG, aB); CFill(QM_X + w - 1, QM_Y, 1, h, aR, aG, aB);
            StarIcon(QM_X + 5, QM_Y + 4);
            CText6(QM_X + 15, QM_Y + 4, T("Favorites"), aR, aG, aB);
            CFill(QM_X + 5, QM_Y + 15, C6Width("Favorites") + 12, 1, aR, aG, aB);

            if (!n) CText6(QM_X + 7, QM_Y + 22, T("Press Y in the menu to star"), tR, tG, tB);
            for (int i = 0; i < n; ++i)
            {
                int y = QM_Y + 20 + i * QM_RH;
                if (i == cursor)
                {
                    CFillBlend(QM_X + 3, y - 1, w - 6, QM_RH, bgLight ? 255 : 0, bgLight ? 255 : 0, bgLight ? 255 : 0, 90);
                    CFill(QM_X + 3, y - 1, 2, QM_RH, aR, aG, aB);
                }
                if (ent[i].it->warp >= 0) // teleport entry: brown box + map-pin icon, it's an action
                {
                    BrownBoxS(QM_X + 6, y + 3);
                    PinIcon(QM_X + 18, y);
                    CText6(QM_X + QM_LBL, y + 4, ent[i].sl, aR, aG, aB); // accent color = "does something"
                }
                else if (ent[i].it->folder >= 0) // folder shortcut: brown box + category icon, opens the folder
                {
                    BrownBoxS(QM_X + 6, y + 3);
                    CategoryIcon(ent[i].it->folder, QM_X + 18, y);
                    CText6(QM_X + QM_LBL, y + 4, ent[i].sl, aR, aG, aB);
                }
                else if (ent[i].it->tool >= 0) // tool shortcut: brown box + its icon, launches the tool
                {
                    BrownBoxS(QM_X + 6, y + 3);
                    DrawSprite(QM_X + 18, y, ToolSprite(ent[i].it->tool), 0);
                    CText6(QM_X + QM_LBL, y + 4, ent[i].sl, aR, aG, aB);
                }
                else if (ent[i].cheat == CH_WP_SLOT) // slot picker: brown box + reticle + auto-name
                {
                    BrownBoxS(QM_X + 6, y + 3);
                    DrawSprite(QM_X + 18, y, 0x11A, 0);
                    char buf[48]; WpName(g_wpSlot, buf, sizeof buf);
                    CText6(QM_X + QM_LBL, y + 4, buf, aR, aG, aB);
                }
                else
                {
                int on = (ent[i].cheat == CH_TOGGLE_AGE) // Toggle Age is self-disabling: lit only during its blink
                       ? (flashCheat == CH_TOGGLE_AGE)
                       : (cheatState[ent[i].cheat] || flashCheat == ent[i].cheat);
                if (IsToggleCheat(ent[i].cheat)) CheckBoxIconS(QM_X + 6, y + 3, on); // on/off toggle -> checkbox
                else                             BrownBoxS(QM_X + 6, y + 3);         // one-shot action -> brown box
                if (ent[i].cheat == CH_TOGGLE_AGE) // dynamic sword icon for the target age
                    DrawSprite(QM_X + 18, y, (LinkPtr() && R32(SAVE_LINKAGE) == 0) ? 0x3B : 0x3C, 0);
                else
                    DrawCheatIcon(QM_X + 18, y, ent[i].cheat); // 16px; handles real sprites AND vector icons
                if (flashCheat == ent[i].cheat)
                {
                    int fx = QM_X + w - 6 - C6Width(flashMsg);
                    if (flashMsg[0] == 'R') CText6(fx, y + 4, flashMsg, aR, aG, aB);
                    else                    CText6(fx, y + 4, flashMsg, gR, gG, gB);
                }
                else
                {
                    const char *lbl = ent[i].sl;
                    if (ent[i].cheat == CH_TOGGLE_AGE) // label = the age you'll become (opposite of current)
                    { int ad = LinkPtr() && R32(SAVE_LINKAGE) == 0; lbl = ad ? T("Child Link") : T("Adult Link"); }
                    CText6(QM_X + QM_LBL, y + 4, lbl, on ? gR : tR, on ? gG : tG, on ? gB : tB);
                }
                }
            }
            Present(); Present();
            changed = 0;
        }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if ((down & BUTTON_DOWN) && n) { cursor = (cursor + 1 < n) ? cursor + 1 : 0; changed = 1; }
        if ((down & BUTTON_UP)   && n) { cursor = (cursor > 0) ? cursor - 1 : n - 1; changed = 1; }
        if (n && ent[cursor].cheat == CH_WP_SLOT && (down & (BUTTON_LEFT | BUTTON_RIGHT))) // ←/→ walk the slots
        {
            g_wpSlot = (down & BUTTON_RIGHT) ? (g_wpSlot + 1) % 9 : (g_wpSlot + 8) % 9;
            g_wpDirty = 1;
            WpName(g_wpSlot, g_wpMsg, sizeof g_wpMsg); QueueToastRaw(g_wpMsg, "");
            changed = 1;
        }
        if ((down & BUTTON_A) && n)
        {
            if (ent[cursor].it->folder >= 0) // folder shortcut: request opening it, then close the quick menu
            { g_openFolder = ent[cursor].it->folder; break; }
            else if (ent[cursor].it->tool >= 0) // tool shortcut: request launching it, then close the quick menu
            { g_openTool = ent[cursor].it->tool; break; }
            else if (ent[cursor].it->warp >= 0) // teleport: warp and hand control back so the scene loads
            {
                int wi = ent[cursor].it->warp;
                if (DoWarp(warps[wi].entrance, warps[wi].age))
                { QueueToastRaw(T(warps[wi].name), T(": WARP")); break; }
                QueueToastRaw("Can't warp right now", "");
                changed = 1;
            }
            else if (ent[cursor].cheat == CH_TOGGLE_AGE) // flip age (instant reload in overworld)
            {
                if (ToggleAge()) break; // reloaded -> resume the game
                flashCheat = CH_TOGGLE_AGE; flashTicks = 40; // self-disabling blink
                changed = 1;
            }
            else if (ent[cursor].cheat == CH_WP_SLOT) // cycle the waypoint slot from the quick menu
            {
                g_wpSlot = (g_wpSlot + 1) % 9; g_wpDirty = 1;
                WpName(g_wpSlot, g_wpMsg, sizeof g_wpMsg); QueueToastRaw(g_wpMsg, "");
                changed = 1;
            }
            else if (ent[cursor].cheat == CH_WP_WARP) // reliable warp to the current slot
            {
                if (WarpToWaypoint()) break; // reloaded -> resume
                changed = 1;
            }
            else if (OneShot(ent[cursor].cheat))
            {
                char sfx[48]; sniprintf(sfx, sizeof sfx, ": %s", g_oneShotMsg);
                QueueToastRaw(ent[cursor].label, sfx);
                flashMsg = g_oneShotMsg;
                flashCheat = ent[cursor].cheat; flashTicks = 50;
                changed = 1;
            }
            else
            {
                cheatState[ent[cursor].cheat] ^= 1;
                QueueToast(ent[cursor].label, cheatState[ent[cursor].cheat]);
                changed = 1;
            }
        }
        if ((down & BUTTON_Y) && n) // unfavorite the selected entry and drop it from the list
        {
            if (ent[cursor].it->warp >= 0)        warpFav[ent[cursor].it->warp] = 0;
            else if (ent[cursor].it->folder >= 0) folderFav[ent[cursor].it->folder] = 0;
            else if (ent[cursor].it->tool >= 0)   toolFav[ent[cursor].it->tool] = 0;
            else                                  favorite[ent[cursor].cheat] = 0;
            favDirty = 1;
            for (int j = cursor; j < n - 1; ++j) ent[j] = ent[j + 1];
            n--;
            if (cursor >= n) cursor = n > 0 ? n - 1 : 0;
            changed = 1;
        }
        if ((down & BUTTON_X) && n && (ent[cursor].it->desc || ent[cursor].it->warp >= 0)) // open the info box
        {
            InfoBox(ent[cursor].it);
            if (g_quitToGame) break; // SELECT dismissed the info box -> to game
            prev = HID_PAD;
            changed = 1;
        }
        if (flashTicks > 0 && --flashTicks == 0) { flashCheat = -1; changed = 1; }
        if (down & (BUTTON_B | BUTTON_SELECT)) break;
    }

    qmLastCursor = cursor; // remember where we were, for the next open
    flashCheat = -1; flashTicks = 0;
    DrainButtons(BUTTON_B | BUTTON_SELECT | BUTTON_A);
    // Only hand back if we're actually going to the game. When a favourite folder or tool was
    // picked, RunMenu() takes over next - releasing here would show one game frame just to
    // recapture it, which is exactly the flicker this hand-off exists to avoid.
    if (g_openFolder < 0 && g_openTool < 0) TopRelease();
    ResumeGame();
    if (favDirty)  { FavSave(); favDirty = 0; }  // persist changes made from the quick menu too
    if (g_wpDirty) { WpSave();  g_wpDirty = 0; }
}
