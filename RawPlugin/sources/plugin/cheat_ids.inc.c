// ===================== Cheat IDs =====================
enum {
    // Movement
    CH_MOONJUMP, CH_FASTMOVE, CH_EPONA_CARROTS, CH_EPONA_CARROTS_ALL, CH_EPONA_MJ,
    CH_WP_SAVE, CH_WP_WARP, CH_WP_SLOT,
    // Battle
    CH_INVINCIBLE, CH_REFILL_HEART, CH_REFILL_MAGIC, CH_HEARTS_MAX, CH_HEARTS_KEEP,
    CH_UNLOCK_MAGIC, CH_UNLOCK_LMAGIC, CH_SPIN, CH_SWORD_GLITCH, CH_STICK_FIRE, CH_NAYRU,
    // Inventory: swords/shields/suits
    CH_SW_KOKIRI, CH_SW_MASTER, CH_SW_BIGGORON, CH_SW_ALL,
    CH_SH_DEKU, CH_SH_HYLIAN, CH_SH_MIRROR, CH_SH_ALL,
    CH_SU_KOKIRI, CH_SU_GORON, CH_SU_ZORA, CH_SU_ALL,
    // Inventory: items
    CH_ARROWS, CH_NUTS, CH_STICKS, CH_BOMBS, CH_BOMBCHU, CH_SLING, CH_EXPLOSIVES,
    CH_GAUNT_BLACK, CH_GAUNT_BLUE, CH_GAUNT_GREEN, CH_GAUNT_PURPLE,
    CH_BOTTLE_ALL, CH_ALL_ITEMS, CH_SKULLTULA, CH_RUPEES,
    // Time
    CH_T_SUNRISE, CH_T_DAY, CH_T_SUNSET, CH_T_NIGHT, CH_T_MOD, CH_RAIN, CH_FREEZE_TIME,
    // Quest
    CH_Q_BEST, CH_Q_STONES, CH_Q_MEDAL, CH_Q_DEFENSE, CH_Q_HEARTS, CH_Q_KEYS, CH_Q_MAPS,
    CH_LEARN_SONGS,
    // Misc
    CH_GIANT, CH_MINI, CH_NORMAL, CH_PAPER,
    CH_CHEST_MANY, CH_HEARTPIECE_MANY, CH_KNIFE_NOBREAK,
    CH_TOGGLE_AGE, CH_SKIP_SONG,
    // Settings
    CH_CFG_TOAST, CH_CFG_AUTOFILL, CH_CFG_QMKEY, CH_CFG_MJKEY, CH_CFG_FMKEY, CH_CFG_HKRESET, CH_CFG_THEME, CH_CFG_LANG,
    NUM_CHEATS
};
static u8 cheatState[NUM_CHEATS];
static u8 favorite[NUM_CHEATS];

// Brief green-check flash so instant (one-shot) cheats give in-menu feedback
static int flashCheat = -1;
static int flashTicks = 0;
static const char *flashMsg = "OK";     // shown next to the cheat during the flash
static const char *g_oneShotMsg = "OK"; // OneShot() sets this: "OK" / "ADDED" / "REMOVED" / ...
// Teleport waypoint: Link's X/Y/Z are 3 consecutive floats at LinkPtr + 0x18 / 0x1C / 0x20.
// Offset found empirically on hardware with the (now-removed) finder: it diffed the actor before
// and after moving and located the float pair that changed with position.
#define WP_XOFF 0x18
// Waypoints: 9 slots. Coords stored as raw float bits (no FP math needed). We also save the scene's
// entrance + room so Warp reloads the scene via the game's respawn mechanism (loads the right room
// + collision), instead of just writing coords (which broke in villages/dungeons).
typedef struct { u32 x, y, z; u16 entrance; u8 room; u8 valid; u8 scene; u8 _pad; } WpSlot;
static WpSlot g_wp[9];
static int    g_wpSlot = 0;
static int    g_wpDirty = 0; // a Save/cycle/reset happened -> persist Waypoints.dat on menu close
static char   g_wpMsg[40];
static int configDirty = 0; // settings changed -> save config on menu close
static int favDirty = 0;    // a favorite toggled -> save Favorites.txt on menu close
static int g_themeIdx = 0, g_themeParchment = 1; // active theme (colors live in CGOLD/... below)

