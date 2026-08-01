static const Item rootItems[] = {
    IT_SEP("CHEATS"),
    IT_FOLDER("Movement",  F_MOVEMENT),
    IT_FOLDER("Battle",    F_BATTLE),
    IT_FOLDER("Inventory", F_INVENTORY),
    IT_FOLDER("Time",      F_TIME),
    IT_FOLDER("Quest",     F_QUEST),
    IT_FOLDER("Misc.",     F_MISC),
    IT_SEP("GUIDES"),
    IT_TOOL_WIDE("100% Checklist", T_CHECKLIST, "Track 100% completion by area, in play order. Auto-fill detects what it safely can from your save; everything else you mark by hand."),
    IT_TOOL("Game Guide",   T_GAMEGUIDE,   "Full Ocarina of Time 3D walkthrough: Young/Adult Link, Master Quest, item locations, secrets. Text by z64central.com, packaged by LowEndC."),
    IT_TOOL("Plugin Guide", T_PLUGINGUIDE, "How to use this plugin: the menu, quick menu, and the Cheat Search / RAM Dumper / Hex Editor tools."),
    IT_SEP("SYSTEM"),
    IT_FOLDER("Tools",     F_TOOLS),
    IT_FOLDER("Settings",  F_SETTINGS),
};
static const Item toolsItems[] = {
    IT_TOOL("Cheat Search", T_SEARCH,  "Search the game's RAM for a value, then narrow it down (greater/less/changed...) to find its address. Poke results directly."),
    IT_TOOL("RAM Dumper",   T_RAMDUMP, "Save a block of the game's memory to a .bin file on the SD card. Pick a start address and size, or pull the address from Cheat Search."),
    IT_TOOL("Hex Editor",   T_HEXEDIT, "Browse memory as a live hex grid and edit any byte on the spot. Jump to an address, or to your Cheat Search result. Read-only regions are protected."),
    IT_TOOL("About",        T_ABOUT,   "Plugin info and credits."),
};
static const Item movementItems[] = {
    IT_CHEAT("Fast Move",                 CH_FASTMOVE,          "Hold {HK} while moving to run much faster (change it in Settings). Careful near loading zones: it can push you out of bounds."),
    IT_CHEAT("MoonJump",                  CH_MOONJUMP,          "Hold {HK} to rise into the air, release to fall (change it in Settings). Mind the fall distance!"),
    IT_CHEAT("Epona MoonJump (hold {L}+{A})", CH_EPONA_MJ,      "Hold {L}+{A} while riding Epona to jump high."),
    IT_CHEAT("Epona Max Carrots",         CH_EPONA_CARROTS,     "Epona's carrots are always full."),
    IT_CHEAT("Epona Carrots All Areas",   CH_EPONA_CARROTS_ALL, "Infinite carrots for Epona in every area."),
    IT_FOLDER("Teleport",                 F_TELEPORT),
    IT_SEP("Waypoint"),
    IT_CHEAT("Waypoint Slot",             CH_WP_SLOT,           "Picks which of the 9 slots (1-9) Save/Warp use. Each slot auto-names itself after the area you saved in. {A} cycles. {START} wipes all slots."),
    IT_CHEAT("Save Position",             CH_WP_SAVE,           "Saves Link's current spot into the active slot. Kept between sessions."),
    IT_CHEAT("Warp to Saved Position",    CH_WP_WARP,           "Warps Link to the spot saved in the active slot. Works anywhere, even dungeons."),
};
static const Item battleItems[] = {
    IT_CHEAT("Invincible",           CH_INVINCIBLE,    "Enemies deal no damage. This one patches game code: SAVE your game before the first use."),
    IT_CHEAT("Keep Hearts Full",     CH_HEARTS_KEEP,   "Constantly refills your hearts (can't die from damage)."),
    IT_CHEAT("Unlock / Max Hearts",  CH_HEARTS_MAX,    "Unlocks all 20 heart containers while active."),
    IT_CHEAT("Refill Hearts (once)", CH_REFILL_HEART,  "Instantly refills all hearts. Applies once."),
    IT_CHEAT("Refill Magic (once)",  CH_REFILL_MAGIC,  "Instantly refills the magic bar. Applies once."),
    IT_CHEAT("Unlock Magic",         CH_UNLOCK_MAGIC,  "Unlocks the magic bar. Applies once."),
    IT_CHEAT("Unlock Large Magic",   CH_UNLOCK_LMAGIC, "Unlocks the large magic bar. Applies once."),
    IT_CHEAT("Spin Attack",          CH_SPIN,          "Spin attack is always fully charged."),
    IT_CHEAT("Sword Glitch",         CH_SWORD_GLITCH,  "Enables the sword glitch state."),
    IT_CHEAT("Sticks Are On Fire",   CH_STICK_FIRE,    "Deku Sticks are always lit on fire."),
    IT_CHEAT("Nayru's Love Always",  CH_NAYRU,         "Keeps the Nayru's Love protection barrier active."),
};
static const Item inventoryItems[] = {
    IT_FOLDER("Swords",  F_SWORDS),
    IT_FOLDER("Shields", F_SHIELDS),
    IT_FOLDER("Suits",   F_SUITS),
    IT_FOLDER("Items",   F_ITEMS),
    IT_PICKER("Inventory Modifier", PK_INV, "Writes the chosen item into the Bottle #3 inventory slot."),
    IT_CHEAT("100 Gold Skulltulas", CH_SKULLTULA,     "Keeps the Gold Skulltula token count at 100."),
    IT_CHEAT("Max Rupees (999)",    CH_RUPEES,        "Keeps your rupees at 999."),
    IT_CHEAT("Unlock All Items",    CH_ALL_ITEMS,     "Fills the inventory with all main items. Applies once."),
};
static const Item swordItems[] = {
    IT_CHEAT("Toggle Kokiri Sword",   CH_SW_KOKIRI,   "Adds or removes the Kokiri Sword from your equipment."),
    IT_CHEAT("Toggle Master Sword",   CH_SW_MASTER,   "Adds or removes the Master Sword from your equipment."),
    IT_CHEAT("Toggle Biggoron Sword", CH_SW_BIGGORON, "Adds or removes the Biggoron Sword from your equipment."),
    IT_CHEAT("Unlock All Swords",     CH_SW_ALL,      "Unlocks all three swords."),
};
static const Item shieldItems[] = {
    IT_CHEAT("Toggle Deku Shield",   CH_SH_DEKU,   "Adds or removes the Deku Shield."),
    IT_CHEAT("Toggle Hylian Shield", CH_SH_HYLIAN, "Adds or removes the Hylian Shield."),
    IT_CHEAT("Toggle Mirror Shield", CH_SH_MIRROR, "Adds or removes the Mirror Shield."),
    IT_CHEAT("Unlock All Shields",   CH_SH_ALL,    "Unlocks all three shields."),
};
static const Item suitItems[] = {
    IT_CHEAT("Toggle Kokiri Tunic", CH_SU_KOKIRI, "Adds or removes the Kokiri Tunic."),
    IT_CHEAT("Toggle Goron Tunic",  CH_SU_GORON,  "Adds or removes the Goron Tunic."),
    IT_CHEAT("Toggle Zora Tunic",   CH_SU_ZORA,   "Adds or removes the Zora Tunic."),
    IT_CHEAT("Unlock All Tunics",   CH_SU_ALL,    "Unlocks all three tunics."),
};
static const Item itemsItems[] = {
    IT_FOLDER("Bottles",         F_BOTTLES),
    IT_FOLDER("Gauntlet Color",  F_GAUNTLET),
    IT_CHEAT("Infinite Arrows",     CH_ARROWS,     "Keeps arrows at 99."),
    IT_CHEAT("Infinite Deku Nuts",  CH_NUTS,       "Keeps Deku Nuts at 99."),
    IT_CHEAT("Infinite Deku Sticks",CH_STICKS,     "Keeps Deku Sticks at 99."),
    IT_CHEAT("Infinite Bombs",      CH_BOMBS,      "Keeps bombs at 99."),
    IT_CHEAT("Infinite Bombchu",    CH_BOMBCHU,    "Keeps Bombchus at 99."),
    IT_CHEAT("Infinite Slingshot",  CH_SLING,      "Keeps slingshot seeds at 99."),
    IT_CHEAT("Infinite Explosives", CH_EXPLOSIVES, "Explosives are never consumed."),
};
static const Item bottleItems[] = {
    IT_PICKER("Bottle #1", PK_BOTTLE1, "Choose this bottle's content. WARNING: don't set 'Locked' on a bottle tied to a key item - it duplicates the slot."),
    IT_PICKER("Bottle #2", PK_BOTTLE2, "Choose this bottle's content. WARNING: don't set 'Locked' on a bottle tied to a key item - it duplicates the slot."),
    IT_PICKER("Bottle #3", PK_BOTTLE3, "Choose this bottle's content. WARNING: don't set 'Locked' on a bottle tied to a key item - it duplicates the slot."),
    IT_CHEAT("Unlock All Bottles (empty)", CH_BOTTLE_ALL, "Turns the three bottle slots into empty bottles. Applies once."),
};
static const Item gauntletItems[] = {
    IT_CHEAT("Black Gauntlet",  CH_GAUNT_BLACK,  "Paints the Golden Gauntlets black. Applies once."),
    IT_CHEAT("Blue Gauntlet",   CH_GAUNT_BLUE,   "Paints the Golden Gauntlets blue. Applies once."),
    IT_CHEAT("Green Gauntlet",  CH_GAUNT_GREEN,  "Paints the Golden Gauntlets green. Applies once."),
    IT_CHEAT("Purple Gauntlet", CH_GAUNT_PURPLE, "Paints the Golden Gauntlets purple. Applies once."),
};
static const Item timeItems[] = {
    IT_CHEAT("Set Sunrise",  CH_T_SUNRISE, "Sets the time of day to sunrise. Applies once."),
    IT_CHEAT("Set Daytime",  CH_T_DAY,     "Sets the time of day to noon. Applies once."),
    IT_CHEAT("Set Sunset",   CH_T_SUNSET,  "Sets the time of day to sunset. Applies once."),
    IT_CHEAT("Set Night",    CH_T_NIGHT,   "Sets the time of day to midnight. Applies once."),
    IT_CHEAT("Sky Always Raining", CH_RAIN, "It always rains.\nEven indoors."),
    IT_CHEAT("Time Modifier ({L}+Up/Down)", CH_T_MOD, "While active: hold {L}+{DP} Up to advance time, {L}+{DP} Down to rewind it."),
    IT_CHEAT("Freeze Time of Day",   CH_FREEZE_TIME, "Locks the current time of day so it stops advancing (also stops the Sun's Song). You can still jump to a set time above."),
};
static const Item questItems[] = {
    IT_CHEAT("Unlock Best Equipment",    CH_Q_BEST,    "Best quiver, bomb bag, scales, seed bag... Applies once."),
    IT_CHEAT("Unlock All Stones",        CH_Q_STONES,  "All 3 Spiritual Stones + Gerudo Token. Applies once."),
    IT_CHEAT("Unlock All Medallions",    CH_Q_MEDAL,   "All 6 Medallions. Applies once."),
    IT_CHEAT("Enhanced Defense",         CH_Q_DEFENSE, "Double defense (damage halved). Applies once."),
    IT_CHEAT("Unlock Heart Pieces",      CH_Q_HEARTS,  "Grants heart pieces. Applies once."),
    IT_CHEAT("Infinite Small Keys",      CH_Q_KEYS,    "Keeps 9 small keys in every dungeon."),
    IT_CHEAT("Map+Compass+BossKey All",  CH_Q_MAPS,    "Map, Compass and Boss Key for every dungeon. Applies once."),
    IT_CHEAT("Learn All Songs",          CH_LEARN_SONGS, "Learns all 12 Ocarina songs. Applies once."),
};
static const Item miscItems[] = {
    IT_CHEAT("Toggle Age (Child/Adult)", CH_TOGGLE_AGE, "Swaps Link between child and adult - the label shows the age you'll become. In an overworld area it changes instantly; in interiors/dungeons it applies on the next area (walk through a door, or use Teleport / Reload current scene). Favorite it for a quick swap from the quick menu."),
    IT_CHEAT("Giant Link",  CH_GIANT,  "Makes Link giant. Use 'Normal Link' to undo. Applies once."),
    IT_CHEAT("Mini Link",   CH_MINI,   "Makes Link tiny. Use 'Normal Link' to undo. Applies once."),
    IT_CHEAT("Normal Link", CH_NORMAL, "Restores Link's normal size. Applies once."),
    IT_CHEAT("Paper Link",  CH_PAPER,  "Makes Link flat like paper. Use 'Normal Link' to undo. Applies once."),
    IT_CHEAT("Open Chest Many Times",    CH_CHEST_MANY,      "Any chest can be opened again and again."),
    IT_CHEAT("Collect Heart Piece Many", CH_HEARTPIECE_MANY, "Overworld heart pieces can be collected repeatedly."),
    IT_CHEAT("Giant Knife Never Breaks", CH_KNIFE_NOBREAK,   "The Giant's Knife never breaks."),
    IT_CHEAT("Skip Song Playback",       CH_SKIP_SONG,       "Skips the little cutscene that replays each Ocarina melody after you finish playing it."),
};
static const Item settingsItems[] = {
    IT_SEP("GENERAL"),
    IT_CHEAT("Change Theme", CH_CFG_THEME, "Recolor every menu live. 26 themes (25 from Gen6) plus the classic Zelda parchment. Your pick is saved."),
    IT_CHEAT("Language", CH_CFG_LANG, "Press {A} to cycle the menu language. Translations load from luma/plugins/<TitleID>/lang/. The Game Guide stays in English."),
    IT_CHEAT("Toggle notifications (toast)", CH_CFG_TOAST, "Shows a small notification in-game when a cheat is toggled."),
    IT_CHEAT("Auto-fill Checklist on open", CH_CFG_AUTOFILL, "When on, the 100% Checklist runs Auto-fill from your save automatically every time you open it."),
    IT_SEP("IN-GAME HOTKEYS"),
    IT_CHEAT("Quick Menu hotkey", CH_CFG_QMKEY, "Press {A} to cycle the button combo that opens the quick menu in game."),
    IT_CHEAT("MoonJump hotkey", CH_CFG_MJKEY, "Press {A} to cycle the button you hold in game to MoonJump."),
    IT_CHEAT("Fast Move hotkey", CH_CFG_FMKEY, "Press {A} to cycle the button you hold in game to Fast Move."),
    IT_CHEAT("Reset hotkeys to default", CH_CFG_HKRESET, "Press {A} to restore the Quick Menu, MoonJump and Fast Move hotkeys to their defaults ({L}+SELECT / {Y} / {X})."),
};

// Teleport folder: one row per warps[] entry (label/desc pulled from warps[] at draw time via the
// index in IT_WARP). Section headers group overworld vs dungeons; keep the indices matching warps[].
static const Item teleportItems[] = {
    IT_WARP_WIDE(NULL, 0,  NULL),               // Reload current scene (full-width, first)
    IT_TPFILTER,                                // category filter (All / Overworld / Dungeons)
    IT_SEP("OVERWORLD"),
    IT_WARP(NULL, 1,  NULL), IT_WARP(NULL, 2,  NULL), IT_WARP(NULL, 3,  NULL),
    IT_WARP(NULL, 4,  NULL), IT_WARP(NULL, 5,  NULL), IT_WARP(NULL, 6,  NULL),
    IT_WARP(NULL, 7,  NULL), IT_WARP(NULL, 8,  NULL), IT_WARP(NULL, 9,  NULL),
    IT_WARP(NULL, 10, NULL), IT_WARP(NULL, 11, NULL), IT_WARP(NULL, 12, NULL),
    IT_WARP(NULL, 13, NULL), IT_WARP(NULL, 14, NULL), IT_WARP(NULL, 15, NULL),
    IT_WARP(NULL, 16, NULL), IT_WARP(NULL, 17, NULL), IT_WARP(NULL, 18, NULL),
    IT_SEP("DUNGEONS"),
    IT_WARP(NULL, 19, NULL), IT_WARP(NULL, 20, NULL), IT_WARP(NULL, 21, NULL),
    IT_WARP(NULL, 22, NULL), IT_WARP(NULL, 23, NULL), IT_WARP(NULL, 24, NULL),
    IT_WARP(NULL, 25, NULL), IT_WARP(NULL, 26, NULL), IT_WARP(NULL, 27, NULL),
    IT_WARP(NULL, 28, NULL), IT_WARP(NULL, 29, NULL),
};

#define FCOUNT(a) (int)(sizeof(a) / sizeof((a)[0]))
static const Folder folders[NUM_FOLDERS] = {
    { "Zelda Ocarina Of Time 3D", rootItems,      FCOUNT(rootItems) },
    { "Movement",                 movementItems,  FCOUNT(movementItems) },
    { "Battle",                   battleItems,    FCOUNT(battleItems) },
    { "Inventory",                inventoryItems, FCOUNT(inventoryItems) },
    { "Swords",                   swordItems,     FCOUNT(swordItems) },
    { "Shields",                  shieldItems,    FCOUNT(shieldItems) },
    { "Suits",                    suitItems,      FCOUNT(suitItems) },
    { "Items",                    itemsItems,     FCOUNT(itemsItems) },
    { "Bottles",                  bottleItems,    FCOUNT(bottleItems) },
    { "Gauntlet Color",           gauntletItems,  FCOUNT(gauntletItems) },
    { "Time",                     timeItems,      FCOUNT(timeItems) },
    { "Quest",                    questItems,     FCOUNT(questItems) },
    { "Misc.",                    miscItems,      FCOUNT(miscItems) },
    { "Tools",                    toolsItems,     FCOUNT(toolsItems) },
    { "Settings",                 settingsItems,  FCOUNT(settingsItems) },
    { "Teleport",                 teleportItems,  FCOUNT(teleportItems) },
};
