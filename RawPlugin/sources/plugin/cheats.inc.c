// ===================== Cheat implementations =====================
static u32 LinkPtr(void) { return R32(G_BASE); }

// ---- Mapped teleport (entrance warp) ----------------------------------------------------------
// OoT3D's scene transition is pure memory writes (mirrors gamestabled's EntranceWarp): set the
// GlobalContext's nextEntranceIndex + fadeOutTransition + linkAgeOnLoad, clear the cutscene, then
// writing sceneLoadFlag = 0x14 makes the game load the new scene when it resumes. See memory
// [[oot3d-warp-teleport-map]]. gSaveContext is the fixed 0x0587958 we already use everywhere.
#define GCTX_BASE      0x0871E840u  // GlobalContext base (USA) - candidate, guarded by GctxValid()
#define GCTX_SCENENUM  0x104        // s16
#define GCTX_LINKAGE   0x5C00       // u8  (0=Adult, 1=Child)
#define GCTX_SCENELOAD 0x5C2D       // s8  transitionTrigger <- writing 0x14 triggers the load
#define GCTX_NEXTENTR  0x5C32       // s16 nextEntranceIndex
#define GCTX_TRANSTYPE 0x5C76       // u8  transitionType (HylianFreddy fork uses 3 for the fade)
#define SAVE_ENTRANCE  0x0587958    // gSaveContext.entranceIndex (s32)
#define SAVE_LINKAGE   0x058795C    // gSaveContext.linkAge (s32)
#define SAVE_CUTSCENE  0x0587960    // gSaveContext.cutsceneIndex (s32)
#define SAVE_NEXTCS    0x0588EF8    // gSaveContext.nextCutsceneIndex (u16)

// Safety: only warp if the GlobalContext base really holds a plausible scene number. A wrong base
// would make the load-trigger write land in random memory and crash, so we refuse instead.
static int GctxValid(void)
{
    if (!LinkPtr()) return 0;                 // not in game (no Link actor)
    u16 sc = R16(GCTX_BASE + GCTX_SCENENUM);
    if (sc >= 0x6A) return 0;                 // real OoT3D scene ids are 0..~0x65
    u16 nx = R16(GCTX_BASE + GCTX_NEXTENTR);  // idle (no pending transition) reads 0xFFFF
    return nx == 0xFFFF;
}

// ---- Scene names (auto-naming for waypoint slots) ----
// scene number (gGlobalContext->sceneNum, 0x00..~0x65) -> short player-facing area name. Same scene
// numbering as N64 OoT. `dungeon` = show the room index too, so two saves in the same dungeon differ.
// Table sourced from the OoT decomp scene_table (see [[oot3d-warp-teleport-map]]); scenes not listed
// fall back to "Slot N". Names kept short so "Name Rn" fits the narrow slot rows.
typedef struct { u8 id; const char *name; u8 dungeon; } SceneInfo;
static const SceneInfo g_scenes[] = {
    // Explorable dungeons (dungeon=1 -> show the room index too). Ids/names from the decomp scene_table.
    { 0x00, "Deku Tree",        1 }, { 0x01, "Dodongo's Cavern", 1 }, { 0x02, "Jabu-Jabu",       1 },
    { 0x03, "Forest Temple",    1 }, { 0x04, "Fire Temple",      1 }, { 0x05, "Water Temple",     1 },
    { 0x06, "Spirit Temple",    1 }, { 0x07, "Shadow Temple",    1 }, { 0x08, "Bottom of the Well",1 },
    { 0x09, "Ice Cavern",       1 }, { 0x0A, "Ganon's Tower",    1 }, { 0x0B, "Gerudo Training",  1 },
    { 0x0C, "Thieves' Hideout", 1 }, { 0x0D, "Ganon's Castle",   1 },
    // Boss rooms (single room -> dungeon=0).
    { 0x11, "Gohma's Lair",     0 }, { 0x12, "King Dodongo",     0 }, { 0x13, "Barinade",        0 },
    { 0x14, "Phantom Ganon",    0 }, { 0x15, "Volvagia",         0 }, { 0x16, "Morpha",          0 },
    { 0x17, "Twinrova",         0 }, { 0x18, "Bongo Bongo",      0 }, { 0x19, "Ganondorf",       0 },
    { 0x4F, "Ganon",            0 },
    // Towns, shops & interiors a player might mark.
    { 0x10, "Treasure Box Shop",0 }, { 0x1B, "Market Entrance",  0 }, { 0x1C, "Market Entrance",  0 },
    { 0x1D, "Market Entrance",  0 }, { 0x20, "Market",           0 }, { 0x21, "Market",          0 },
    { 0x22, "Market",           0 }, { 0x23, "ToT Exterior",     0 }, { 0x24, "ToT Exterior",    0 },
    { 0x25, "ToT Exterior",     0 }, { 0x2C, "Bazaar",           0 }, { 0x2D, "Kokiri Shop",     0 },
    { 0x2E, "Goron Shop",       0 }, { 0x2F, "Zora Shop",        0 }, { 0x33, "Happy Mask Shop", 0 },
    { 0x34, "Link's House",     0 }, { 0x38, "Lakeside Lab",     0 }, { 0x3A, "Dampe's Hut",     0 },
    { 0x3B, "Great Fairy",      0 }, { 0x3C, "Fairy Fountain",   0 }, { 0x3D, "Great Fairy",     0 },
    { 0x3E, "Grotto",           0 }, { 0x42, "Shooting Gallery", 0 }, { 0x43, "Temple of Time",  0 },
    { 0x44, "Chamber of Sages", 0 }, { 0x48, "Windmill",         0 }, { 0x49, "Fishing Pond",    0 },
    { 0x4B, "Bombchu Bowling",  0 }, { 0x50, "House of Skulltula",0 },
    // Overworld areas.
    { 0x51, "Hyrule Field",     0 }, { 0x52, "Kakariko Village", 0 }, { 0x53, "Graveyard",       0 },
    { 0x54, "Zora's River",     0 }, { 0x55, "Kokiri Forest",    0 }, { 0x56, "Forest Meadow",   0 },
    { 0x57, "Lake Hylia",       0 }, { 0x58, "Zora's Domain",    0 }, { 0x59, "Zora's Fountain",  0 },
    { 0x5A, "Gerudo Valley",    0 }, { 0x5B, "Lost Woods",       0 }, { 0x5C, "Desert Colossus",  0 },
    { 0x5D, "Gerudo Fortress",  0 }, { 0x5E, "Wasteland",        0 }, { 0x5F, "Hyrule Castle",   0 },
    { 0x60, "Death Mtn Trail",  0 }, { 0x61, "Death Mtn Crater", 0 }, { 0x62, "Goron City",      0 },
    { 0x63, "Lon Lon Ranch",    0 }, { 0x64, "Outside Ganon",    0 },
};
#define NUM_SCENES (int)(sizeof(g_scenes)/sizeof(g_scenes[0]))

static const SceneInfo *SceneLookup(u8 scene)
{
    for (int i = 0; i < NUM_SCENES; ++i) if (g_scenes[i].id == scene) return &g_scenes[i];
    return 0;
}

// Fill `buf` with the display label for waypoint slot `s`. Empty/unknown -> "Slot N"; a saved slot ->
// "N: <area>" (plus " Rn" room index for dungeons, so two saves in one dungeon differ). `cap` bounds
// the write. Names are short, so the number prefix keeps every slot distinct even in the same area.
static void WpName(int s, char *buf, int cap)
{
    const SceneInfo *si = (s >= 0 && s < 9 && g_wp[s].valid) ? SceneLookup(g_wp[s].scene) : 0;
    if (!si) { sniprintf(buf, (size_t)cap, "Slot %d", s + 1); return; }
    if (si->dungeon) sniprintf(buf, (size_t)cap, "%d. %s R%d", s + 1, si->name, g_wp[s].room);
    else             sniprintf(buf, (size_t)cap, "%d. %s", s + 1, si->name);
}

// Perform the warp. entrance 0xFFFF = "reload current scene" (uses the live entrance index).
// age < 0 keeps Link's current age. Returns 0 (and does nothing) if it isn't safe to warp.
static int DoWarp(u16 entrance, int age)
{
    if (!GctxValid()) return 0;
    if (entrance == 0xFFFF) entrance = (u16)R32(SAVE_ENTRANCE); // reload: current entrance
    // Keep the age that's PENDING (linkAgeOnLoad), not the currently-spawned one, so a Toggle Age
    // done just before a warp/reload carries through instead of being reset to the live age.
    if (age < 0) age = (int)R8(GCTX_BASE + GCTX_LINKAGE);
    W8 (GCTX_BASE + GCTX_LINKAGE, (u8)(age & 1));
    W32(SAVE_CUTSCENE, 0);
    W16(SAVE_NEXTCS, 0xFFEF);
    W16(GCTX_BASE + GCTX_NEXTENTR, entrance);
    W8 (GCTX_BASE + GCTX_TRANSTYPE, 3);
    W8 (GCTX_BASE + GCTX_SCENELOAD, 0x14); // last: this is what actually starts the load
    return 1;
}

static void ScaleLink(u32 bits, int all3)
{
    u32 p = LinkPtr();
    if (!p) return;
    p += 0x64;
    W32(p, bits);
    if (all3) { W32(p + 4, bits); W32(p + 8, bits); }
}

static void TriggerNibble(u32 addr, int high, u8 bits) // XOR equipment bit (original Trigger*)
{
    u8 b = R8(addr);
    if (high) b = (u8)((b & 0x0F) | ((b & 0xF0) ^ (bits << 4)));
    else      b = (u8)((b & 0xF0) | ((b & 0x0F) ^ bits));
    W8(addr, b);
}

// Invincible: 4-instruction code patch (original saves + restores)
static void SetInvincible(int on)
{
    static const u32 addr[4]  = { 0x0035D398, 0x0035D3A8, 0x00352E24, 0x00352E28 };
    static const u32 patch[4] = { 0xE3A00000, 0xEA000000, 0xE1D504B2, 0xE1A00000 };
    static u32 orig[4];
    static int saved = 0, applied = 0, rwx = 0;

    if (on == applied) return;
    if (on)
    {
        if (!rwx) { svcControlProcess(CUR_PROCESS_HANDLE, PROCESSOP_SET_MMU_TO_RWX, 0, 0); rwx = 1; }
        if (!saved) { for (int i = 0; i < 4; ++i) orig[i] = R32(addr[i]); saved = 1; }
        for (int i = 0; i < 4; ++i) W32(addr[i], patch[i]);
    }
    else if (saved)
        for (int i = 0; i < 4; ++i) W32(addr[i], orig[i]);

    svcFlushEntireDataCache();
    svcInvalidateEntireInstructionCache();
    applied = on;
}

// One-shot cheats: applied instantly when selected in the menu. Returns 1 if id is a one-shot.
static int OneShot(int id)
{
    u32 p;
    g_oneShotMsg = "OK"; // default; the equipment toggles below override with ADDED/REMOVED
    switch (id)
    {
        case CH_REFILL_HEART:  W16(0x058799C, 0x140); return 1;
        case CH_REFILL_MAGIC:  W8(0x058799F, R8(0x05879A8) ? 0x60 : 0x30); return 1;
        case CH_UNLOCK_MAGIC:  W8(0x05879A6, 0x60); return 1;
        case CH_UNLOCK_LMAGIC: W8(0x05879A8, 0x01); return 1;

        // Equipment toggles: TriggerNibble XORs the bit, so re-selecting removes. Report which
        // way it went (ADDED / REMOVED) by reading the bit back, so the feedback is unambiguous.
        case CH_SW_KOKIRI:   TriggerNibble(0x0587A0E, 0, 0x1); g_oneShotMsg = (R8(0x0587A0E)&0x01)?"ADDED":"REMOVED"; return 1;
        case CH_SW_MASTER:   TriggerNibble(0x0587A0E, 0, 0x2); g_oneShotMsg = (R8(0x0587A0E)&0x02)?"ADDED":"REMOVED"; return 1;
        case CH_SW_BIGGORON: TriggerNibble(0x0587A0E, 0, 0x4); g_oneShotMsg = (R8(0x0587A0E)&0x04)?"ADDED":"REMOVED"; return 1;
        case CH_SW_ALL:      W8(0x0587A0E, (u8)((R8(0x0587A0E) & 0xF0) | 0x07)); return 1;
        case CH_SH_DEKU:     TriggerNibble(0x0587A0E, 1, 0x1); g_oneShotMsg = (R8(0x0587A0E)&0x10)?"ADDED":"REMOVED"; return 1;
        case CH_SH_HYLIAN:   TriggerNibble(0x0587A0E, 1, 0x2); g_oneShotMsg = (R8(0x0587A0E)&0x20)?"ADDED":"REMOVED"; return 1;
        case CH_SH_MIRROR:   TriggerNibble(0x0587A0E, 1, 0x4); g_oneShotMsg = (R8(0x0587A0E)&0x40)?"ADDED":"REMOVED"; return 1;
        case CH_SH_ALL:      W8(0x0587A0E, (u8)((R8(0x0587A0E) & 0x0F) | 0x70)); return 1;
        case CH_SU_KOKIRI:   TriggerNibble(0x0587A0F, 0, 0x1); g_oneShotMsg = (R8(0x0587A0F)&0x01)?"ADDED":"REMOVED"; return 1;
        case CH_SU_GORON:    TriggerNibble(0x0587A0F, 0, 0x2); g_oneShotMsg = (R8(0x0587A0F)&0x02)?"ADDED":"REMOVED"; return 1;
        case CH_SU_ZORA:     TriggerNibble(0x0587A0F, 0, 0x4); g_oneShotMsg = (R8(0x0587A0F)&0x04)?"ADDED":"REMOVED"; return 1;
        case CH_SU_ALL:      W8(0x0587A0F, 0x07); return 1;

        // Learn every Ocarina song: set the song bits in the quest word (bits 6-17), confirmed by
        // save-diffing. 0x0587A14 bits 6-7 (Minuet/Bolero), 0x0587A15 all 8 (Serenade..Sun),
        // 0x0587A16 bits 0-1 (Song of Time/Storms). Medallion & stone bits in those bytes are left
        // untouched by the masks.
        case CH_LEARN_SONGS:
            W8(0x0587A14, (u8)(R8(0x0587A14) | 0xC0));
            W8(0x0587A15, 0xFF);
            W8(0x0587A16, (u8)(R8(0x0587A16) | 0x03));
            return 1;

        case CH_GAUNT_BLACK:  W16(0x0587A10, 0xE500 + 0xDB); return 1;
        case CH_GAUNT_BLUE:   W16(0x0587A10, 0xE500 + 0x9B); return 1;
        case CH_GAUNT_GREEN:  W16(0x0587A10, 0xE500 + 0x5B); return 1;
        case CH_GAUNT_PURPLE: W16(0x0587A10, 0xE500 + 0x1B); return 1;

        case CH_BOTTLE_ALL: W16(0x05879F6, 0x1414); W8(0x05879F8, 0x14); return 1;

        case CH_ALL_ITEMS:
        {
            static const u8 buf[] = {
                0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x08,0x09,0x0B,
                0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x18,0x18,
                0x18,0x18,0x37,0x2B,0x45,0x46,0x1E,0x28,0x28,0x32
            };
            memcpy((void *)0x5879E4, buf, sizeof(buf));
            return 1;
        }
        case CH_WP_SAVE:
        {
            u32 pl = LinkPtr();
            if (!pl) { g_oneShotMsg = "NO LINK"; return 1; }
            int s = g_wpSlot;
            g_wp[s].x = R32(pl + WP_XOFF + 0);
            g_wp[s].y = R32(pl + WP_XOFF + 4);
            g_wp[s].z = R32(pl + WP_XOFF + 8);
            g_wp[s].entrance = (u16)R32(0x0587958);   // gSaveContext.entranceIndex
            g_wp[s].room     = R8(0x08723470);         // roomCtx.curRoom.num
            g_wp[s].scene    = (u8)(R16(GCTX_BASE + GCTX_SCENENUM) & 0xFF); // sceneNum -> auto-name the slot
            g_wp[s].valid    = 1;
            g_wpDirty = 1;
            char nm[48]; WpName(s, nm, sizeof nm);   // "N: <area>" now that scene is captured
            sniprintf(g_wpMsg, sizeof g_wpMsg, "%s set", nm);
            g_oneShotMsg = g_wpMsg;
            return 1;
        }
        // CH_WP_WARP is NOT a one-shot: it reloads the scene, so it's handled in the menu loop
        // (WarpToWaypoint) which then resumes the game.

        case CH_T_SUNRISE: W16(0x0587964, 0x8001); return 1;
        case CH_T_DAY:     W16(0x0587964, 0x680E); return 1;
        case CH_T_SUNSET:  W16(0x0587964, 0xC001); return 1;
        case CH_T_NIGHT:   W16(0x0587964, 0x0000); return 1;

        case CH_Q_BEST:    W32(0x0587A10, 0x184D8); return 1;
        case CH_Q_STONES:  W8(0x0587A16, 0x7C); return 1;
        case CH_Q_MEDAL:   W8(0x0587A14, 0x3F); return 1;
        case CH_Q_DEFENSE: W8(0x05879A9, 0x01); return 1;
        case CH_Q_HEARTS:  W8(0x0587A17, 0x30); return 1;
        case CH_Q_MAPS:
            for (int i = 0; i < 6; ++i) W32(0x0587A18 + i, 0x07070707);
            return 1;

        case CH_GIANT:  ScaleLink(0x3CA3D70A, 1); return 1;
        case CH_MINI:   ScaleLink(0x3B23D70A, 1); return 1;
        case CH_NORMAL: ScaleLink(0x3C23D70A, 1); return 1;
        case CH_PAPER:  p = LinkPtr(); if (p) W32(p + 0x64, 0x3AD3D70A); return 1;
    }
    return 0;
}

// Wait for buttons to be physically released before handing control back to the game. The game is
// paused while a plugin screen is up; the instant it resumes it reads the live pad, so a still-held
// B/SELECT would fire in-game (B = sword swing). Capped (~2s) so a stuck pad can never hang the
// console with the game paused - which presents as a dead 3DS. Every wait-for-release in the plugin
// must go through here; a bare `while (HID_PAD)` on the raw hardware register can spin forever.
static void DrainButtons(u32 mask)
{
    for (int i = 0; i < 125 && (HID_PAD & mask); ++i)
        svcSleepThread(16 * 1000 * 1000);
}

// Continuous cheats: applied every tick while the menu is CLOSED (game running).
static void ApplyCheats(void)
{
    u32 p = LinkPtr(), q;
    u32 pad = HID_PAD;

    // --- Movement ---
    if (cheatState[CH_MOONJUMP] && (pad & hotKeys[mjKey].mask) && p) W16(p + 0x77, 0xCB40);
    if (cheatState[CH_FASTMOVE] && p)
    {
        static u32 jump = 0;
        if (pad & hotKeys[fmKey].mask)
        {
            if (jump < 3) { W16(p + 0x77, 0xCB40); jump++; }
            else          W32(p + 0x222C, 0x41A00000);
        }
        else jump = 0;
    }
    if (cheatState[CH_EPONA_CARROTS] && p) W8(p + 0x134, 0x5);
    if (cheatState[CH_EPONA_CARROTS_ALL] && p)
    { q = R32(p + 0x134); if (q) W8(q + 0xE9C, 0x5); }
    if (cheatState[CH_EPONA_MJ] && (pad & BUTTON_L1) && (pad & BUTTON_A) && p)
    { q = R32(p + 0x134); if (q) W16(q + 0x66, 0x4222); }

    // --- Battle ---
    SetInvincible(cheatState[CH_INVINCIBLE]);
    if (cheatState[CH_HEARTS_MAX])  W16(0x058799A, 0x140);
    if (cheatState[CH_HEARTS_KEEP]) W16(0x058799C, 0x140);
    if (cheatState[CH_SPIN] && p)         W16(p + 0x2252, 0x3F80);
    if (cheatState[CH_SWORD_GLITCH] && p) W8(p + 0x2237, 0x01);
    if (cheatState[CH_STICK_FIRE] && p)   W8(p + 0x2258, 0xFF);
    if (cheatState[CH_NAYRU]) W16(0x0588EAA, 0xFFFF);

    // --- Items ---
    if (cheatState[CH_ARROWS])  W8(0x0587A01, 0x63);
    if (cheatState[CH_NUTS])    W8(0x05879FF, 0x63);
    if (cheatState[CH_STICKS])  W8(0x05879FE, 0x63);
    if (cheatState[CH_BOMBS]) { W8(0x0587A00, 0x63); W8(0x08001AB4, 0x63); }
    if (cheatState[CH_BOMBCHU]){ W8(0x0587A06, 0x63); W8(0x08001AD0, 0x63); }
    if (cheatState[CH_SLING])   W8(0x0587A04, 0x63);
    if (cheatState[CH_EXPLOSIVES])
    { q = R32(0x0FFFE538); if (q) W8(q + 0x910, 0x0); }
    if (cheatState[CH_SKULLTULA]) { W8(0x0587A40, 0x64); W8(0x05C2280, 0x64); }
    if (cheatState[CH_RUPEES])    W16(0x05879A0, 0x3E7);

    // --- Time ---
    if (cheatState[CH_T_MOD] && (pad & BUTTON_L1))
    {
        if (pad & BUTTON_UP)   W16(0x0587964, (u16)(R16(0x0587964) + 0x20));
        if (pad & BUTTON_DOWN) W16(0x0587964, (u16)(R16(0x0587964) - 0x20));
    }
    if (cheatState[CH_RAIN]) W8(0x08721AAF, 0xFF);
    // Freeze time of day: capture dayTime (0x0587964) when turned on, then hold it there each frame.
    {
        static u8 wasFreeze = 0; static u16 frozen = 0;
        if (cheatState[CH_FREEZE_TIME]) { if (!wasFreeze) frozen = R16(0x0587964); W16(0x0587964, frozen); }
        wasFreeze = cheatState[CH_FREEZE_TIME];
    }

    // --- Quest ---
    if (cheatState[CH_Q_KEYS])
        for (int i = 0; i < 0x10; ++i) W32(0x0587A2C + i, 0x09090909);

    // --- Misc ---
    if (cheatState[CH_CHEST_MANY])      W32(0x08720A78, 0x0);
    if (cheatState[CH_HEARTPIECE_MANY]) W16(0x08720A84, 0x0);
    if (cheatState[CH_KNIFE_NOBREAK]) W8(0x05879A2, 0xFF);
    // Skip the ocarina "song playback" cutscene: msgMode 18/19 -> 23 ends it (GlobalContext+0x2A90).
    if (cheatState[CH_SKIP_SONG] && p) { u8 m = R8(0x087212D0); if (m == 18 || m == 19) W8(0x087212D0, 23); }
}
