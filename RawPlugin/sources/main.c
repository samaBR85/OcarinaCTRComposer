#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "plgldr.h"
#include "csvc.h"
#include "common.h"
#include "font6x10.h"
#include "sysfont.h"
#include "topbg.h"
#include "botbg.h"
#include "sprites.h"
#include "kbkeys.h"
#include "glyphs.h"
#include "logo.h"
#include "guide.h"
#include "themes.h"

// Public release version + build counter. Bump the build number every build (the tag is the
// on-screen confirmation that the newest .3gx is loaded); the build count also doubles as the
// "many iterations" badge. Keep the two "90"s in sync.
#define PLUGIN_VER "v1.0.1 build 171" // full string - About screen and pause box (have room)
#define PLUGIN_TAG "b171"             // compact tag - cramped menu title bar

static Handle   thread;
static Handle   onProcessExitEvent, resumeExitEvent;
#define PLG_STACK_SIZE 0x4000            // 16KB (printf + hid + deep calls need room)
static u8       stack[PLG_STACK_SIZE] ALIGN(8);

// Our worker thread is spawned with a raw svcCreateThread, so its libctru
// ThreadVars (TLS+0) is never initialized. Newlib/hid/fs read magic@0 and
// reent@0x8 from there and svcBreak on a bad magic. Seed it once.
extern struct _reent *_impure_ptr;
static void InitThreadVars(void)
{
    volatile u32 *tv = (volatile u32 *)getThreadLocalStorage();
    tv[0] = 0x21545624;            // THREADVARS_MAGIC
    tv[1] = 0;                     // thread_ptr
    tv[2] = (u32)_impure_ptr;      // reent  (global newlib reentrancy)
    tv[3] = 0;                     // tls_tp
    tv[4] = 0;                     // fs_magic (0 = use the global fs session)
}

// --- Direct game-memory access (plugin runs inside the game process) ---
static inline void  W8(u32 a, u8 v)   { *(volatile u8  *)a = v; }
static inline void  W16(u32 a, u16 v) { *(volatile u16 *)a = v; }
static inline void  W32(u32 a, u32 v) { *(volatile u32 *)a = v; }
static inline u8    R8(u32 a)          { return *(volatile u8  *)a; }
static inline u16   R16(u32 a)         { return *(volatile u16 *)a; }
static inline u32   R32(u32 a)         { return *(volatile u32 *)a; }

#define G_BASE  0x005A2E3C   // base pointer to Link actor (OoT3D EUR)

// ===================== LCD registers =====================
#define LCD_TOP     0x10400400
#define LCD_BOT     0x10400500
#define LCD_FBA1    0x68
#define LCD_FBA2    0x6C
#define LCD_FORMAT  0x70
#define LCD_SELECT  0x78
#define LCD_STRIDE  0x90
#define TOP_W  400
#define TOP_H  240
#define BOT_W  320
#define BOT_H  240

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

// ===================== Config persistence (SD, fs:USER) =====================
// OoT3D has fs:USER in its ACL (unlike ir:rst), so a raw plugin can read/write
// the SD card. We persist the star favorites + toast toggle + quick-menu hotkey
// next to the .3gx. Cheats themselves are NOT persisted (never auto-enable a
// code patch like Invincible on boot).
#define CFG_PATH "/luma/plugins/0004000000033500/Settings.cfg"
#define LANG_DIR  "/luma/plugins/0004000000033500/lang/"
#define CFG_MAGIC 0x544F4F5A // 'ZOOT'
#define CFG_VER   13 // v13: MoonJump/Fast Move default hotkeys changed to {Y}/{X}

static FS_Archive cfgArchive;
static int fsReady;

// APPEND-ONLY (since v8): only add new fields at the END, never reorder/remove. ConfigLoad migrates
// older files instead of resetting them, so a new field must (a) bump CFG_VER and (b) get a default
// in ConfigLoad's `if (version < N)` ladder if its default is non-zero.
typedef struct {
    u32 magic, version;
    u8  toast;
    u8  qmCombo;
    u8  theme;
    u8  lang;
    u8  mjKey;   // MoonJump hotkey (index into hotKeys[])
    u8  fmKey;   // Fast Move hotkey (index into hotKeys[])
    u8  autoFill; // 1 = auto-fill the Checklist from the save each time it opens
} ConfigBlob; // favorites live in Favorites.txt now, so the cheat list can change without resetting them

static void FsBootInit(void)
{
    if (fsReady) return;
    if (R_FAILED(fsInit())) return;
    if (R_FAILED(FSUSER_OpenArchive(&cfgArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) return;
    fsReady = 1;
}

static void ConfigApply(const ConfigBlob *c); // fwd
static void ConfigFill(ConfigBlob *c);        // fwd
static void ApplyTheme(int idx);              // fwd (defined with the color state)
static void LangLoad(void);                   // fwd (defined below with localization)

// ===================== Localization (gettext-style: English source = key) =====================
// UI strings are wrapped in T("English"). At runtime T() looks the English text
// up in a table loaded from luma/plugins/<TID>/lang/<Language>.txt and returns
// the translation, or the English string unchanged if there's no entry (so a
// partial translation just shows English for the missing lines). The GAME GUIDE
// text is never routed through T() and stays English.
//
// Language 0 = English uses no file (identity). File names match kLangNames.
static const char *kLangNames[] = {
    "English", "Francais", "Deutsch", "Italiano", "Espanol", "Portugues",
};
#define NUM_LANGS (int)(sizeof(kLangNames)/sizeof(kLangNames[0]))
// Menu label shown for each language (in its own language, ASCII-safe filename above).
static const char *kLangLabels[] = {
    "English", "Francais", "Deutsch", "Italiano", "Espanol", "Portugues (BR)",
};
static int g_langIdx = 0;
static int g_firstRun = 1;   // no Settings.cfg yet -> show the language chooser once

#define LANG_MAX 512
static char       *g_langBuf = NULL;         // whole file, parsed in place
static const char *g_enKey[LANG_MAX];
static const char *g_trVal[LANG_MAX];
static int         g_langCount = 0;

// Translate: return the localized form of an English UI string, or the input.
static const char *T(const char *en)
{
    if (!en || !g_langCount) return en;
    for (int i = 0; i < g_langCount; ++i)
        if (g_enKey[i] == en || strcmp(g_enKey[i], en) == 0) return g_trVal[i];
    return en;
}

// Load lang/<name>.txt into the table. Parses "English=Translation" lines,
// '#'/';'/blank lines ignored. English (idx 0) or any failure => identity table.
static void LangLoad(void)
{
    g_langCount = 0;
    if (g_langBuf) { free(g_langBuf); g_langBuf = NULL; }
    if (g_langIdx <= 0 || g_langIdx >= NUM_LANGS) return;

    FsBootInit();
    if (!fsReady) return;

    char path[128];
    int p = 0;
    for (const char *s = LANG_DIR; *s; ++s) path[p++] = *s;
    for (const char *s = kLangNames[g_langIdx]; *s; ++s) path[p++] = *s;
    for (const char *s = ".txt"; *s; ++s) path[p++] = *s;
    path[p] = 0;

    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0)))
        return;
    u64 sz64 = 0;
    FSFILE_GetSize(f, &sz64);
    u32 sz = (u32)sz64;
    if (sz == 0 || sz > 256 * 1024) { FSFILE_Close(f); return; }
    g_langBuf = (char *)malloc(sz + 1);
    if (!g_langBuf) { FSFILE_Close(f); return; }
    u32 got = 0;
    Result r = FSFILE_Read(f, &got, 0, g_langBuf, sz);
    FSFILE_Close(f);
    if (R_FAILED(r) || got == 0) { free(g_langBuf); g_langBuf = NULL; return; }
    g_langBuf[got] = 0;

    // Skip a UTF-8 BOM if present.
    char *q = g_langBuf;
    if ((u8)q[0] == 0xEF && (u8)q[1] == 0xBB && (u8)q[2] == 0xBF) q += 3;

    while (*q && g_langCount < LANG_MAX)
    {
        char *line = q;
        while (*q && *q != '\n') q++;
        char *eol = q;
        if (*q) q++;                       // step past '\n'
        if (eol > line && eol[-1] == '\r') eol[-1] = 0;
        *eol = 0;

        if (*line == '#' || *line == ';' || *line == 0) continue;
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;          // no '=' -> not a mapping
        *eq = 0;
        char *val = eq + 1;
        if (*line == 0 || *val == 0) continue;  // empty key/value: keep English
        g_enKey[g_langCount] = line;
        g_trVal[g_langCount] = val;
        g_langCount++;
    }
}

// Which languages actually have a file on the SD card. English (idx 0) is embedded, so always
// available; the rest need lang/<Name>.txt to exist. If the whole lang/ folder is missing, every
// non-English probe fails and they all read as unavailable. Cheap enough to re-run on picker entry.
static u8 g_langAvail[NUM_LANGS];
static void LangProbeAvail(void)
{
    g_langAvail[0] = 1;
    FsBootInit();
    for (int i = 1; i < NUM_LANGS; ++i)
    {
        g_langAvail[i] = 0;
        if (!fsReady) continue;
        char path[128]; int p = 0;
        for (const char *s = LANG_DIR; *s; ++s) path[p++] = *s;
        for (const char *s = kLangNames[i]; *s; ++s) path[p++] = *s;
        for (const char *s = ".txt"; *s; ++s) path[p++] = *s;
        path[p] = 0;
        Handle f;
        if (R_SUCCEEDED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0)))
        { g_langAvail[i] = 1; FSFILE_Close(f); }
    }
}

// ===================== SD-loaded guides (per language) =====================
// The English Game Guide + Plugin Guide stay embedded (guide.h / PLUGIN_PAGES)
// as the always-available fallback. For other languages we load translated
// copies from luma/plugins/<TID>/guide/<Name>/{game,plugin}.txt at runtime, so
// the binary stays small and guides are editable without recompiling.
// File format: "%C Category" starts a category, "%P Page" starts a page; every
// other line is body text (kept verbatim, including its newlines).
#define GUIDE_DIR "/luma/plugins/0004000000033500/guide/"
#define SDG_MAXCATS  12
#define SDG_MAXPAGES 96

static char      *g_ggBuf;                    // game.txt (parsed in place)
static GuidePage  g_ggPagesBuf[SDG_MAXPAGES];
static GuideCat   g_ggCatsBuf[SDG_MAXCATS];
static int        g_ggNCats;                  // 0 -> use embedded GUIDE_CATS
static char      *g_pgBuf;                    // plugin.txt
static GuidePage  g_pgPagesBuf[32];
static int        g_pgNPages;                 // 0 -> use embedded PLUGIN_PAGES

// Read a whole SD text file into a fresh malloc'd, NUL-terminated buffer (or NULL).
static char *ReadSdText(const char *path, u32 maxsz)
{
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0)))
        return NULL;
    u64 sz64 = 0; FSFILE_GetSize(f, &sz64);
    u32 sz = (u32)sz64;
    if (sz == 0 || sz > maxsz) { FSFILE_Close(f); return NULL; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { FSFILE_Close(f); return NULL; }
    u32 got = 0;
    Result r = FSFILE_Read(f, &got, 0, buf, sz);
    FSFILE_Close(f);
    if (R_FAILED(r) || got == 0) { free(buf); return NULL; }
    buf[got] = 0;
    return buf;
}

// Parse "%C/%P" markers in place into cats[]/pages[]. Bodies are terminated by
// NUL at the byte before the next marker. Returns category count.
static int ParseGuideCats(char *buf, GuideCat *cats, GuidePage *pages, int maxc, int maxp)
{
    int nc = 0, np = 0;
    GuideCat *cc = NULL; GuidePage *lp = NULL;
    char *p = buf;
    if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) p += 3; // skip BOM
    while (*p)
    {
        char *line = p;
        while (*p && *p != '\n') p++;
        char *eol = p; if (*p) p++;
        int isC = (line[0] == '%' && line[1] == 'C' && line[2] == ' ');
        int isP = (line[0] == '%' && line[1] == 'P' && line[2] == ' ');
        if (!(isC || isP)) continue;                 // body line: leave as-is
        if (lp && line > buf) line[-1] = 0;          // terminate previous page body
        char *t = eol; if (t > line && t[-1] == '\r') t--; *t = 0; // terminate title
        if (isC) { if (nc < maxc) { cc = &cats[nc++]; cc->title = line + 3; cc->pages = &pages[np]; cc->nPages = 0; } lp = NULL; }
        else if (cc && np < maxp) { lp = &pages[np++]; lp->title = line + 3; lp->body = p; cc->nPages++; }
    }
    return nc;
}

// Same, but flat pages only (Plugin Guide has no categories).
static int ParseGuidePages(char *buf, GuidePage *pages, int maxp)
{
    int np = 0; GuidePage *lp = NULL;
    char *p = buf;
    if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) p += 3;
    while (*p)
    {
        char *line = p;
        while (*p && *p != '\n') p++;
        char *eol = p; if (*p) p++;
        if (!(line[0] == '%' && line[1] == 'P' && line[2] == ' ')) continue;
        if (lp && line > buf) line[-1] = 0;
        char *t = eol; if (t > line && t[-1] == '\r') t--; *t = 0;
        if (np < maxp) { lp = &pages[np++]; lp->title = line + 3; lp->body = p; }
    }
    return np;
}

// Load translated guides for the current language (English uses the embedded copy).
static void GuideLoad(void)
{
    g_ggNCats = 0; g_pgNPages = 0;
    if (g_ggBuf) { free(g_ggBuf); g_ggBuf = NULL; }
    if (g_pgBuf) { free(g_pgBuf); g_pgBuf = NULL; }
    if (g_langIdx <= 0 || g_langIdx >= NUM_LANGS) return;
    FsBootInit();
    if (!fsReady) return;

    char path[192];
    for (int which = 0; which < 2; ++which)
    {
        int n = 0;
        for (const char *s = GUIDE_DIR; *s; ++s) path[n++] = *s;
        for (const char *s = kLangNames[g_langIdx]; *s; ++s) path[n++] = *s;
        path[n++] = '/';
        for (const char *s = (which == 0 ? "game.txt" : "plugin.txt"); *s; ++s) path[n++] = *s;
        path[n] = 0;

        char *buf = ReadSdText(path, 320 * 1024);
        if (!buf) continue;
        if (which == 0)
        {
            g_ggBuf = buf;
            g_ggNCats = ParseGuideCats(buf, g_ggCatsBuf, g_ggPagesBuf, SDG_MAXCATS, SDG_MAXPAGES);
            if (g_ggNCats <= 0) { free(buf); g_ggBuf = NULL; g_ggNCats = 0; }
        }
        else
        {
            g_pgBuf = buf;
            g_pgNPages = ParseGuidePages(buf, g_pgPagesBuf, 32);
            if (g_pgNPages <= 0) { free(buf); g_pgBuf = NULL; g_pgNPages = 0; }
        }
    }
}

static const GuideCat  *GG_Cats(int *n);   // fwd (defined with the Plugin Guide, after PLUGIN_PAGES)
static const GuidePage *PG_Pages(int *n);  // fwd

static void ConfigLoad(void)
{
    FsBootInit();
    if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, CFG_PATH), FS_OPEN_READ, 0)))
        return;
    // Migrate instead of reset: the blob is append-only from v8, so an older/shorter file still
    // has valid theme/lang/hotkey fields. Read what's there (zero-filled), accept any version in
    // [8, current], and give each field a NEWER version didn't have its proper default below.
    ConfigBlob c;
    memset(&c, 0, sizeof c);
    u32 got = 0;
    Result r = FSFILE_Read(f, &got, 0, &c, sizeof(c));
    FSFILE_Close(f);
    if (R_SUCCEEDED(r) && got >= 12 && c.magic == CFG_MAGIC && c.version >= 8 && c.version <= CFG_VER)
    {
        if (c.version < 9)  { c.mjKey = 2; c.fmKey = 1; } // MoonJump/Fast Move hotkeys added in v9
        if (c.version < 10) { c.autoFill = 1; }           // auto-fill-on-open added in v10 (default on)
        if (c.version < 13) { c.mjKey = 2; c.fmKey = 1; } // v13: default hotkeys re-based to {Y}/{X} (only touches users still on the old A/L defaults or older)
        ConfigApply(&c);
    }
}

static void ConfigSave(void)
{
    FsBootInit();
    if (!fsReady) return;
    ConfigBlob c;
    ConfigFill(&c);
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, CFG_PATH),
                                 FS_OPEN_WRITE | FS_OPEN_CREATE, 0)))
        return;
    u32 wrote = 0;
    FSFILE_SetSize(f, sizeof(ConfigBlob));
    FSFILE_Write(f, &wrote, 0, &c, sizeof(c), FS_WRITE_FLUSH);
    FSFILE_Close(f);
}

// ===================== Quick menu hotkey (rebindable in Settings) =====================
// NOTE: ZL/ZR (N3DS) were tried and abandoned: OoT3D reads the C-stick via the
// ir:USER Circle Pad Pro protocol, and neither passive location of an ir:rst
// mapping (game never maps one) nor driving ir:rst ourselves delivered data.
typedef struct { const char *name; const char *plain; u32 pad; } QmCombo;
static const QmCombo qmCombos[] = {
    { "{L}+SELECT", "L+SELECT", BUTTON_L1 | BUTTON_SELECT }, // name: glyph-render; plain: toast/small font
    { "{R}+SELECT", "R+SELECT", BUTTON_R1 | BUTTON_SELECT },
};
#define NUM_QMCOMBOS (int)(sizeof(qmCombos) / sizeof(qmCombos[0]))
static int qmCombo = 0;

// Rebindable in-game hotkeys (MoonJump / Fast Move). Glyph tokens render as button icons.
// B is intentionally excluded (it swings the sword in game).
typedef struct { const char *glyph; u32 mask; } HotKey;
static const HotKey hotKeys[] = {
    { "{A}", BUTTON_A }, { "{X}", BUTTON_X }, { "{Y}", BUTTON_Y },
    { "{L}", BUTTON_L1 }, { "{R}", BUTTON_R1 },
    // N3DS ZL/ZR were investigated (build 129): they're not in the raw HID register we poll, only in
    // the ir:rst/CPP shared memory the game maps privately - unreachable from this overlay without
    // initialising ir:rst ourselves (tried before, unreliable on N3DS). Left out on purpose.
};
#define NUM_HOTKEYS (int)(sizeof(hotKeys) / sizeof(hotKeys[0]))
static int mjKey = 2; // default {Y}
static int fmKey = 1; // default {X}

// config <-> live state (defined here now that all the state exists)
static void ConfigApply(const ConfigBlob *c)
{
    cheatState[CH_CFG_TOAST] = c->toast ? 1 : 0;
    cheatState[CH_CFG_AUTOFILL] = c->autoFill ? 1 : 0;
    qmCombo = (c->qmCombo < NUM_QMCOMBOS) ? c->qmCombo : 0;
    mjKey = (c->mjKey < NUM_HOTKEYS) ? c->mjKey : 2;
    fmKey = (c->fmKey < NUM_HOTKEYS) ? c->fmKey : 1;
    ApplyTheme(c->theme);
    g_langIdx = (c->lang < NUM_LANGS) ? c->lang : 0;
    LangLoad();
    LangProbeAvail(); // know which languages have SD files (for the red "unavailable" marker)
    GuideLoad();
    g_firstRun = 0; // a valid config exists -> not the first launch
}
static void ConfigFill(ConfigBlob *c)
{
    c->magic = CFG_MAGIC; c->version = CFG_VER;
    c->toast = cheatState[CH_CFG_TOAST];
    c->qmCombo = (u8)qmCombo;
    c->theme = (u8)g_themeIdx;
    c->lang = (u8)g_langIdx;
    c->mjKey = (u8)mjKey;
    c->fmKey = (u8)fmKey;
    c->autoFill = cheatState[CH_CFG_AUTOFILL];
}

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

// ---- D-pad auto-repeat (typematic) -------------------------------------------------------------
// Menu loops normally use edge detection (pad & ~prev), so a held button fires once. This wraps
// that: hold a D-pad direction and, after a short delay, it keeps firing so long lists scroll
// without mashing. ONLY the D-pad repeats - A/B/X/Y/START/SELECT stay edge-only (else a held A
// would toggle a cheat over and over). Each loop passes its own `prev`; `hold` is shared (only one
// loop runs at a time, and it resets whenever no direction is held).
#define AR_DIRS  (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)
#define AR_DELAY 20   // frames a direction is held before auto-repeat kicks in (~320ms @ 16ms/frame)
#define AR_RATE  4    // then repeat every this many frames (~65ms)
static int g_arHold = 0;
static u32 ARepeat(u32 pad, u32 *prev, int *hold)
{
    u32 down = pad & ~*prev;                 // genuine edges (any button)
    u32 dir  = pad & AR_DIRS;
    if (dir && dir == (*prev & AR_DIRS))     // same direction(s) still held since last frame
    {
        if (++(*hold) >= AR_DELAY && ((*hold - AR_DELAY) % AR_RATE) == 0) down |= dir;
    }
    else *hold = 0;                          // direction changed or released -> restart the delay
    *prev = pad;
    return down;
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

// ===================== Pickers (bottle contents / inventory item) =====================
typedef struct { const char *name; u8 val; } PickOpt;
typedef struct { const char *title; const PickOpt *opts; int count; u32 addr; } Picker;

static const PickOpt bottleOpts[] = {
    { "Red Potion", 0x15 }, { "Green Potion", 0x16 }, { "Blue Potion", 0x17 },
    { "Fairy", 0x18 }, { "Fish", 0x19 }, { "Milk (2 doses)", 0x1A }, { "Milk (1 dose)", 0x1F },
    { "Letter", 0x1B }, { "Blue Flame", 0x1C }, { "Insect", 0x1D }, { "Soul", 0x1E },
    { "Spirit", 0x20 }, { "Empty Bottle", 0x14 }, { "Locked (careful!)", 0xFF },
};

static const PickOpt invOpts[] = {
    { "Deku Stick", 0x00 }, { "Deku Nut", 0x01 }, { "Bomb", 0x02 }, { "Fairy Bow", 0x03 },
    { "Fire Arrow", 0x04 }, { "Din's Fire", 0x05 }, { "Fairy Slingshot", 0x06 },
    { "Fairy Ocarina", 0x07 }, { "Ocarina of Time", 0x08 }, { "Bombchu", 0x09 },
    { "Hookshot", 0x0A }, { "Longshot", 0x0B }, { "Ice Arrow", 0x0C }, { "Farore's Wind", 0x0D },
    { "Boomerang", 0x0E }, { "Lens of Truth", 0x0F }, { "Magic Beans", 0x10 },
    { "Megaton Hammer", 0x11 }, { "Light Arrow", 0x12 }, { "Nayru's Love", 0x13 },
    { "Weird Egg", 0x21 }, { "Cucco", 0x22 }, { "Zelda's Letter", 0x23 },
    { "Keaton Mask", 0x24 }, { "Skull Mask", 0x25 }, { "Spooky Mask", 0x26 },
    { "Bunny Hood", 0x27 }, { "Goron Mask", 0x28 }, { "Zora Mask", 0x29 },
    { "Gerudo Mask", 0x2A }, { "Mask of Truth", 0x2B }, { "No Mask", 0x2C },
    { "Pocket Egg", 0x2D }, { "Pocket Cucco", 0x2E }, { "Cojiro", 0x2F },
    { "Odd Mushroom", 0x30 }, { "Odd Poultice", 0x31 }, { "Poacher's Saw", 0x32 },
    { "Broken Goron Sword", 0x33 }, { "Prescription", 0x34 }, { "Eyeball Frog", 0x35 },
    { "World Finest Eyedrops", 0x36 }, { "Claim Check", 0x37 },
    { "Fairy Bow (Fire)", 0x38 }, { "Fairy Bow (Ice)", 0x39 }, { "Fairy Bow (Light)", 0x3A },
    { "Kokiri Sword", 0x3B }, { "Master Sword", 0x3C }, { "Giant's Knife", 0x3D },
    { "Deku Shield", 0x3E }, { "Hylian Shield", 0x3F }, { "Mirror Shield", 0x40 },
    { "Kokiri Tunic", 0x41 }, { "Goron Tunic", 0x42 }, { "Zora Tunic", 0x43 },
    { "Iron Boots", 0x45 }, { "Biggoron's Sword", 0x7C }, { "Empty", 0xFF },
};

enum { PK_BOTTLE1, PK_BOTTLE2, PK_BOTTLE3, PK_INV, NUM_PICKERS };
static const Picker pickers[NUM_PICKERS] = {
    { "Bottle #1",          bottleOpts, 14, 0x05879F6 },
    { "Bottle #2",          bottleOpts, 14, 0x05879F7 },
    { "Bottle #3",          bottleOpts, 14, 0x05879F8 },
    { "Inventory Modifier", invOpts,    58, 0x05879F8 },
};

// Mapped teleport destinations. Entrance indices are OoT3D values (from gamestabled's entrances.c,
// NOT the N64 wrong-warp tables). age -1 = keep Link's current age. See [[oot3d-warp-teleport-map]].
typedef struct { const char *name; u16 entrance; s8 age; const char *desc; } Warp;
static const Warp warps[] = {
    /* 0 */  { "Reload current scene",   0xFFFF, -1, "Reloads the area you're in. The safest warp - use it first to confirm teleport works on your game." },
    // --- Overworld & towns (indices 1..18) ---
    /* 1 */  { "Kokiri Forest",          0x00EE, -1, "Warp to Kokiri Forest." },
    /* 2 */  { "Lost Woods",             0x011E, -1, "Warp to the Lost Woods." },
    /* 3 */  { "Link's House",           0x0272, -1, "Warp inside Link's house." },
    /* 4 */  { "Hyrule Field",           0x00CD, -1, "Warp to Hyrule Field." },
    /* 5 */  { "Market",                 0x00B1, -1, "Warp to the Castle Town Market." },
    /* 6 */  { "Temple of Time",         0x0053, -1, "Warp to the Temple of Time." },
    /* 7 */  { "Lon Lon Ranch",          0x0157, -1, "Warp to Lon Lon Ranch." },
    /* 8 */  { "Kakariko Village",       0x00DB, -1, "Warp to Kakariko Village." },
    /* 9 */  { "Death Mtn Trail",       0x013D, -1, "Warp to the Death Mountain Trail." },
    /* 10 */ { "Death Mtn Crater",      0x0147, -1, "Warp to Death Mountain Crater." },
    /* 11 */ { "Goron City",             0x014D, -1, "Warp to Goron City." },
    /* 12 */ { "Zora's River",           0x00EA, -1, "Warp to Zora's River." },
    /* 13 */ { "Zora's Domain",          0x0108, -1, "Warp to Zora's Domain." },
    /* 14 */ { "Zora's Fountain",        0x010E, -1, "Warp to Zora's Fountain." },
    /* 15 */ { "Lake Hylia",             0x0102, -1, "Warp to Lake Hylia." },
    /* 16 */ { "Gerudo Valley",          0x0117, -1, "Warp to Gerudo Valley." },
    /* 17 */ { "Wasteland",              0x0130, -1, "Warp to the Haunted Wasteland." },
    /* 18 */ { "Desert Colossus",        0x0123, -1, "Warp to the Desert Colossus." },
    // --- Dungeons (indices 19..29) ---
    /* 19 */ { "Deku Tree",              0x0000, -1, "Warp inside the Great Deku Tree." },
    /* 20 */ { "Dodongo's Cavern",       0x0004, -1, "Warp inside Dodongo's Cavern." },
    /* 21 */ { "Jabu-Jabu",             0x0028, -1, "Warp inside Jabu-Jabu's Belly." },
    /* 22 */ { "Forest Temple",          0x0169, -1, "Warp inside the Forest Temple." },
    /* 23 */ { "Fire Temple",            0x0165, -1, "Warp inside the Fire Temple." },
    /* 24 */ { "Water Temple",           0x0010, -1, "Warp inside the Water Temple." },
    /* 25 */ { "Shadow Temple",          0x0037, -1, "Warp inside the Shadow Temple." },
    /* 26 */ { "Spirit Temple",          0x0082, -1, "Warp inside the Spirit Temple." },
    /* 27 */ { "Ice Cavern",             0x0088, -1, "Warp inside the Ice Cavern." },
    /* 28 */ { "Gerudo Training",        0x0008, -1, "Warp inside the Gerudo Training Ground." },
    /* 29 */ { "Ganon's Castle",         0x0467, -1, "Warp inside Ganon's Castle." },
};
#define NUM_WARPS (int)(sizeof(warps)/sizeof(warps[0]))
static u8 warpFav[NUM_WARPS]; // teleport destinations starred for the quick menu (own Favorites lines, '@'-prefixed)

// ===================== Menu model (folders) =====================
typedef struct { const char *label; int cheat; int folder; int picker; const char *desc; int tool; int warp; u8 wide; } Item;
typedef struct { const char *title; const Item *items; int count; } Folder;

enum {
    F_ROOT, F_MOVEMENT, F_BATTLE, F_INVENTORY, F_SWORDS, F_SHIELDS, F_SUITS,
    F_ITEMS, F_BOTTLES, F_GAUNTLET, F_TIME, F_QUEST, F_MISC, F_TOOLS, F_SETTINGS,
    F_TELEPORT, NUM_FOLDERS
};

// tool screens
enum { T_SEARCH, T_RAMDUMP, T_HEXEDIT, T_ABOUT, T_GAMEGUIDE, T_PLUGINGUIDE, T_CHECKLIST, NUM_TOOLS };
static void ToolRun(int t); // fwd

#define IT_CHEAT(lbl, ch, d)   { lbl, ch, -1, -1, d, -1, -1, 0 }
#define IT_FOLDER(lbl, fl)     { lbl, -1, fl, -1, NULL, -1, -1, 0 }
#define IT_PICKER(lbl, pk, d)  { lbl, -1, -1, pk, d, -1, -1, 0 }
#define IT_TOOL(lbl, tl, d)    { lbl, -1, -1, -1, d, tl, -1, 0 }
#define IT_TOOL_WIDE(lbl, tl, d) { lbl, -1, -1, -1, d, tl, -1, 1 } // HOME only: spans both columns, still selectable
#define IT_WARP(lbl, wp, d)    { lbl, -1, -1, -1, d, -1, wp, 0 }  // teleport destination (index into warps[])
#define IT_WARP_WIDE(lbl, wp, d) { lbl, -1, -1, -1, d, -1, wp, 1 } // full-width teleport row
#define IT_TPFILTER            { "Filter", -1, -1, -1, NULL, -1, -2, 1 } // Teleport category filter (warp==-2, wide)
#define IT_SEP(lbl)            { lbl, -2, -1, -1, NULL, -1, -1, 0 } // non-selectable section header
#define IS_SEP(it)             ((it)->cheat == -2)

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

static u8  folderFav[NUM_FOLDERS]; // folders starred for the quick menu (own Favorites lines, '#'-prefixed)
static u8  toolFav[NUM_TOOLS];     // tools starred for the quick menu (own Favorites lines, '&'-prefixed)
static int g_openFolder = -1;      // quick menu sets this to a folder id to open after it closes
static int g_openTool   = -1;      // quick menu sets this to a tool id to launch after it closes
// stable keys for tool favorites in Favorites.txt (index = tool id; order must match the T_* enum)
static const char *kToolKeys[NUM_TOOLS] = {
    "Cheat Search", "RAM Dumper", "Hex Editor", "About", "Game Guide", "Plugin Guide", "100% Checklist"
};
static int g_tpFilter = 0;         // Teleport category filter: 0=all, 1=overworld, 2=dungeons

// An item is hidden when the Teleport filter excludes its category. Only ever true inside F_TELEPORT;
// every other folder shows everything, so render/nav/scroll are unchanged there. Reload (warp 0) and
// the filter row (warp -2) are always visible; overworld = warps 1..18, dungeons = warps 19..29.
static int ItemHidden(int folderIdx, const Item *it)
{
    if (folderIdx != F_TELEPORT || g_tpFilter == 0) return 0;
    if (it->warp == -2 || it->warp == 0) return 0;
    if (it->warp >= 0) { int dun = it->warp >= 19; return (g_tpFilter == 1) ? dun : !dun; }
    if (IS_SEP(it))    { int dunSep = it->label[0] == 'D'; return (g_tpFilter == 1) ? dunSep : !dunSep; }
    return 0;
}
// A row the cursor must skip over: a section header OR a filtered-out item.
static int NavSkip(int folderIdx, int c)
{ const Folder *f = &folders[folderIdx]; return IS_SEP(&f->items[c]) || ItemHidden(folderIdx, &f->items[c]); }
// Visible position (0-based) of a raw item index, skipping hidden items - used for scroll math.
static int VisPos(int folderIdx, int rawIdx)
{ int vp = 0; const Folder *f = &folders[folderIdx];
  for (int i = 0; i < f->count && i < rawIdx; ++i) if (!ItemHidden(folderIdx, &f->items[i])) vp++;
  return vp; }

// Favorites persist in their OWN file, keyed by the cheat's stable English label - NOT by enum
// index inside the versioned config blob. Positional storage broke every time the cheat list
// changed (add/remove a cheat -> NUM_CHEATS changes -> the whole config is rejected on load and
// favorites reset). Keying by label means adding/removing/reordering cheats never loses favorites;
// only renaming a single cheat's label drops that one favorite.
#define FAV_PATH "/luma/plugins/0004000000033500/Favorites.txt"
static const char *LabelForCheat(int ch)
{
    if (ch < 0) return NULL;
    for (int f = 0; f < NUM_FOLDERS; ++f)
        for (int i = 0; i < folders[f].count; ++i)
        {
            const Item *it = &folders[f].items[i];
            if (it->cheat == ch && it->folder < 0 && it->picker < 0 && it->tool < 0)
                return it->label;
        }
    return NULL;
}
static int CheatForLabel(const char *lbl)
{
    for (int f = 0; f < NUM_FOLDERS; ++f)
        for (int i = 0; i < folders[f].count; ++i)
        {
            const Item *it = &folders[f].items[i];
            if (it->cheat >= 0 && it->folder < 0 && it->picker < 0 && it->tool < 0 &&
                strcmp(it->label, lbl) == 0)
                return it->cheat;
        }
    return -1;
}
static void FavSave(void)
{
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, FAV_PATH),
                                 FS_OPEN_WRITE | FS_OPEN_CREATE, 0)))
        return;
    u32 off = 0, wrote;
    for (int c = 0; c < NUM_CHEATS; ++c)
    {
        if (!favorite[c]) continue;
        const char *lbl = LabelForCheat(c);
        if (!lbl) continue;
        char line[80]; int n = sniprintf(line, sizeof line, "%s\n", lbl);
        FSFILE_Write(f, &wrote, off, line, (u32)n, FS_WRITE_FLUSH); off += wrote;
    }
    // teleport favorites: '@'-prefixed, keyed by the warp's stable English name
    for (int wpi = 0; wpi < NUM_WARPS; ++wpi)
    {
        if (!warpFav[wpi]) continue;
        char line[80]; int n = sniprintf(line, sizeof line, "@%s\n", warps[wpi].name);
        FSFILE_Write(f, &wrote, off, line, (u32)n, FS_WRITE_FLUSH); off += wrote;
    }
    // folder favorites: '#'-prefixed, keyed by the folder's stable English title
    for (int fi = 0; fi < NUM_FOLDERS; ++fi)
    {
        if (!folderFav[fi]) continue;
        char line[80]; int n = sniprintf(line, sizeof line, "#%s\n", folders[fi].title);
        FSFILE_Write(f, &wrote, off, line, (u32)n, FS_WRITE_FLUSH); off += wrote;
    }
    // tool favorites: '&'-prefixed, keyed by the tool's stable English name
    for (int ti = 0; ti < NUM_TOOLS; ++ti)
    {
        if (!toolFav[ti]) continue;
        char line[80]; int n = sniprintf(line, sizeof line, "&%s\n", kToolKeys[ti]);
        FSFILE_Write(f, &wrote, off, line, (u32)n, FS_WRITE_FLUSH); off += wrote;
    }
    FSFILE_SetSize(f, off);
    FSFILE_Close(f);
}
static void FavLoad(void)
{
    memset(favorite, 0, sizeof(favorite));
    memset(warpFav, 0, sizeof(warpFav));
    memset(folderFav, 0, sizeof(folderFav));
    memset(toolFav, 0, sizeof(toolFav));
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, FAV_PATH), FS_OPEN_READ, 0)))
        return;
    u64 sz64 = 0; FSFILE_GetSize(f, &sz64);
    u32 sz = (u32)sz64;
    if (sz == 0 || sz > 8 * 1024) { FSFILE_Close(f); return; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { FSFILE_Close(f); return; }
    u32 got = 0;
    Result r = FSFILE_Read(f, &got, 0, buf, sz);
    FSFILE_Close(f);
    if (R_FAILED(r) || !got) { free(buf); return; }
    buf[got] = 0;
    char *p = buf;
    while (*p)
    {
        char *line = p;
        while (*p && *p != '\n') p++;
        char *eol = p; if (*p) p++;
        if (eol > line && eol[-1] == '\r') eol[-1] = 0;
        *eol = 0;
        if (line[0] == '@') // teleport favorite: match the name against warps[]
        {
            for (int wpi = 0; wpi < NUM_WARPS; ++wpi)
            {
                const char *a = line + 1, *b = warps[wpi].name; int k = 0;
                while (a[k] && b[k] && a[k] == b[k]) k++;
                if (!a[k] && !b[k]) { warpFav[wpi] = 1; break; }
            }
        }
        else if (line[0] == '#') // folder favorite: match the title against folders[]
        {
            for (int fi = 0; fi < NUM_FOLDERS; ++fi)
            {
                const char *a = line + 1, *b = folders[fi].title; int k = 0;
                while (a[k] && b[k] && a[k] == b[k]) k++;
                if (!a[k] && !b[k]) { folderFav[fi] = 1; break; }
            }
        }
        else if (line[0] == '&') // tool favorite: match against the stable tool keys
        {
            for (int ti = 0; ti < NUM_TOOLS; ++ti)
            {
                const char *a = line + 1, *b = kToolKeys[ti]; int k = 0;
                while (a[k] && b[k] && a[k] == b[k]) k++;
                if (!a[k] && !b[k]) { toolFav[ti] = 1; break; }
            }
        }
        else if (line[0]) { int c = CheatForLabel(line); if (c >= 0) favorite[c] = 1; }
    }
    free(buf);
}

// Waypoints persist in their own binary file (9 slots + the selected slot). Saved on menu close
// when g_wpDirty is set (a Save, a slot cycle, or a reset).
#define WP_PATH  "/luma/plugins/0004000000033500/Waypoints.dat"
#define WP_MAGIC 0x3250574F // 'OWP2' - bumped when the scene field was added (old files ignored)
typedef struct { u32 magic; u32 slot; WpSlot wp[9]; } WpFile;
static void WpSave(void)
{
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, WP_PATH),
                                 FS_OPEN_WRITE | FS_OPEN_CREATE, 0)))
        return;
    WpFile b; b.magic = WP_MAGIC; b.slot = (u32)g_wpSlot; memcpy(b.wp, g_wp, sizeof(g_wp));
    u32 wrote = 0;
    FSFILE_SetSize(f, sizeof(b));
    FSFILE_Write(f, &wrote, 0, &b, sizeof(b), FS_WRITE_FLUSH);
    FSFILE_Close(f);
}
static void WpLoad(void)
{
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, WP_PATH), FS_OPEN_READ, 0)))
        return;
    WpFile b; memset(&b, 0, sizeof b);
    u32 got = 0;
    Result r = FSFILE_Read(f, &got, 0, &b, sizeof(b));
    FSFILE_Close(f);
    if (R_SUCCEEDED(r) && got == sizeof(b) && b.magic == WP_MAGIC)
    {
        memcpy(g_wp, b.wp, sizeof(g_wp));
        g_wpSlot = (b.slot < 9) ? (int)b.slot : 0;
    }
}

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

// ===================== Toast =====================
static char toastMsg[48];
static volatile int toastTicks;

static void QueueToastRaw(const char *label, const char *suffix)
{
    if (!cheatState[CH_CFG_TOAST]) return;
    int i = 0;
    while (label[i] && i < 38) { toastMsg[i] = label[i]; i++; }
    for (int j = 0; suffix[j] && i < 46; ++j) toastMsg[i++] = suffix[j];
    toastMsg[i] = 0;
    toastTicks = 625; // ~2.5s at the 4ms toast tick
}
static void QueueToast(const char *label, int on) { QueueToastRaw(label, on ? ": ON" : ": OFF"); }

static void ToastTick(void)
{
    if (toastTicks <= 0) return;
    toastTicks--;

    int w = C6BtnWidth(toastMsg) + 10, h = 14; // glyph-aware: cheat labels may carry {A}/{L} tokens
    int x0 = TOP_W - 6 - w, y0 = TOP_H - 6 - h;

    CFill(x0, y0, w, h, BG); // theme background: keeps text readable on light & dark themes
    CFill(x0, y0, w, 1, GOLD); CFill(x0, y0 + h - 1, w, 1, GOLD);
    CFill(x0, y0, 1, h, GOLD); CFill(x0 + w - 1, y0, 1, h, GOLD);
    CText6Btn(x0 + 5, y0 + 2, toastMsg, INK);

    FbInfo f = GetFb(0);
    BlitTopRect(&f, x0, y0, w, h);
}

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

// ===================== Tools =====================
// These run from inside RunMenu, so the game is already paused (memory stable).

// ---- Touch keypad (bottom screen). hid:USER is in the game's ACL, so unlike
// ir:rst we CAN read the touch panel. HID runs in its own process, so touch
// keeps updating even while the game threads are frozen. ----
// IMPORTANT: do NOT use libctru's hidInit() — on New 3DS it calls irrstInit()
// internally (hidShouldUseIrrst()), and ir:rst conflicts with OoT3D and FREEZES
// the game (+ blocks the HOME button). We do a minimal manual hid:USER init:
// open the service, map the shared memory, read touch directly. No ir:rst.
static int   hidReady;
static vu32 *hidShmem;

static void KbInit(void)
{
    if (hidReady) return;
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

    Handle srv = 0;
    if (R_FAILED(srvGetServiceHandle(&srv, "hid:USER")) &&
        R_FAILED(srvGetServiceHandle(&srv, "hid:SPVR"))) return;

    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(0xA, 0, 0); // HIDUSER_GetIPCHandles
    Result r = svcSendSyncRequest(srv);
    if (R_SUCCEEDED(r)) r = (Result)cmd[1];
    Handle mem = 0, ev[5] = { 0 };
    if (R_SUCCEEDED(r)) { mem = cmd[3]; for (int i = 0; i < 5; ++i) ev[i] = cmd[4 + i]; }
    svcCloseHandle(srv);
    for (int i = 0; i < 5; ++i) if (ev[i]) svcCloseHandle(ev[i]); // events unused
    if (R_FAILED(r) || !mem) return;

    vu32 *sh = (vu32 *)mappableAlloc(0x2B0);
    if (!sh) { svcCloseHandle(mem); return; }
    if (R_FAILED(svcMapMemoryBlock(mem, (u32)sh, MEMPERM_READ, MEMPERM_DONTCARE)))
    { svcCloseHandle(mem); return; }

    hidShmem = sh;
    hidReady = 1;
}

// Read the touch panel directly from HID shared memory (layout per libctru:
// touch section at word 42, index at 42+4, entries {pos,valid} at 42+8+Id*2).
static int HidTouch(int *px, int *py)
{
    if (!hidShmem) return 0;
    u32 id = hidShmem[42 + 4]; if (id > 7) id = 7;
    u32 packed = hidShmem[42 + 8 + id * 2];
    u32 valid  = hidShmem[42 + 8 + id * 2 + 1];
    if (px) *px = (int)(packed & 0xFFFF);
    if (py) *py = (int)((packed >> 16) & 0xFFFF);
    return valid != 0;
}

// hit-test a touch against a key rect
static int KbHit(int tx, int ty, int x, int y, int w, int h)
{ return tx >= x && tx < x + w && ty >= y && ty < y + h; }

// one key, styled by kind. kinds: 0 num, 1 del/clr, 2 hex-on, 3 hex-off,
// 4 toggle(gold), 5 OK(green), 6 Cancel(red)
static void KbKey(int x, int y, int w, int h, const char *label, int kind, int hot)
{
    u8 br, bg, bb, tr, tg, tb;
    switch (kind)
    {
        case 1:  br=40; bg=33; bb=20; tr=246; tg=236; tb=210; break; // Del/Clr
        case 3:  br=34; bg=28; bb=17; tr=140; tg=130; tb=104; break; // hex off
        case 4:  br=46; bg=62; bb=42; tr=236; tg=200; tb=120; break; // toggle
        case 5:  br=36; bg=70; bb=34; tr=180; tg=240; tb=170; break; // OK
        case 6:  br=74; bg=42; bb=30; tr=240; tg=180; tb=160; break; // Cancel
        default: br=54; bg=44; bb=26; tr=246; tg=236; tb=210; break; // num / hex on
    }
    if (hot) { br += 30; bg += 30; bb += 22; }
    CFill(x, y, w, h, br, bg, bb);
    CFill(x, y, w, 1, GOLD); CFill(x, y + h - 1, w, 1, GOLD);
    CFill(x, y, 1, h, GOLD); CFill(x + w - 1, y, 1, h, GOLD);
    int tw = CTextWidth(label);
    CText(x + (w - tw) / 2, y + (h - 15) / 2, label, tr, tg, tb, 1);
}

// Layout B: phone numpad + hex block + bottom OK/Cancel bar.
// codes: 0-15 = digit value; 20 Del; 21 Clr; 22 toggle; 23 OK; 24 Cancel
typedef struct { int x, y, w, h, code; const char *lab; } KbBtn;

static int KbBuild(KbBtn *b, int hex)
{
    // Coordinates measured from the user's 320x240 reference layout (kbd/position guide). Every key is
    // a sprite (see kbkeys.h). kw/kh = 52x30, pitch 57 x / 35 y.
    int n = 0, kw = 52, kh = 30, XP = 57, YP = 35, x0 = 16, y0 = 53;
    static const int   pc[12] = { 1,2,3, 4,5,6, 7,8,9, 20,0,21 };
    static const char *pl[12] = { "1","2","3","4","5","6","7","8","9","Del","0","Clr" };
    for (int i = 0; i < 12; ++i)
    { int c = i % 3, r = i / 3;
      b[n++] = (KbBtn){ x0 + c*XP, y0 + r*YP, kw, kh, pc[i], pl[i] }; }

    int hx = 196;
    static const int   hc[6] = { 10,11,12,13,14,15 };
    static const char *hl[6] = { "A","B","C","D","E","F" };
    for (int i = 0; i < 6; ++i)
    { int c = i % 2, r = i / 2;
      b[n++] = (KbBtn){ hx + c*XP, y0 + r*YP, kw, kh, hc[i], hl[i] }; }

    b[n++] = (KbBtn){ hx, 158, 109, 30, 22, hex ? "HEX" : "DEC" };
    b[n++] = (KbBtn){ 51,  198, 96, 32, 23, "OK" };
    b[n++] = (KbBtn){ 173, 198, 96, 32, 24, "Cancel" };
    return n;
}

// Touch keypad entry (decimal, with HEX toggle). Returns value; *cancel on Cancel.
static u32 EnterNum(const char *title, u32 initial, int *cancel)
{
    KbInit();
    *cancel = 0;
    u32 val = initial;
    int hex = 0, changed = 1, hotCode = -1;
    int touchPrev = HidTouch(0, 0); // ignore a touch already held on entry

    while (1)
    {
        KbBtn b[24];
        int nb = KbBuild(b, hex);

        if (changed)
        {
            // dim frozen bottom frame as backdrop
            for (int y = 0; y < BOT_H; ++y)
                for (int x = 0; x < BOT_W; ++x)
                {
                    u8 *p = CPix(x, y);
                    if (savedBotValid)
                    { u16 v = savedBot[y * BOT_W + x];
                      p[0] = (u8)((((v>>11)&31)<<3)/3); p[1] = (u8)((((v>>5)&63)<<2)/3); p[2] = (u8)(((v&31)<<3)/3); }
                    else { p[0] = p[1] = p[2] = 12; }
                }
            CText(14, 8, title, GOLD, 1);
            char disp[40];
            if (hex) siprintf(disp, "0x%lX", (unsigned long)val);
            else     siprintf(disp, "%lu  (0x%lX)", (unsigned long)val, (unsigned long)val);
            CFill(12, 30, 296, 18, 18, 13, 7);
            CText(18, 31, disp, INK, 0);

            for (int i = 0; i < nb; ++i)
            {
                int c = b[i].code, x = b[i].x, y = b[i].y, w = b[i].w, h = b[i].h;
                int hot = (hotCode == c);
                if (c <= 9)        // digit sprite (kb_digits[value]: 00.png=0 .. 09.png=9)
                    DrawKbSprite(x, y, w, h, kb_digits[c], KB_KEY_W, KB_KEY_H, 0);
                else if (c <= 15)  // hex A-F sprite, dimmed while in DEC mode (disabled)
                    DrawKbSprite(x, y, w, h, kb_hex[c - 10], KB_KEY_W, KB_KEY_H, hex ? 0 : 150);
                else if (c == 20)  // Del (backspace) sprite
                    DrawKbSprite(x, y, w, h, kb_del, KB_KEY_W, KB_KEY_H, 0);
                else if (c == 21)  // Clr sprite
                    DrawKbSprite(x, y, w, h, kb_clr, KB_KEY_W, KB_KEY_H, 0);
                else if (c == 22)  // DEC/HEX toggle sprite (label reflects the CURRENT mode)
                    DrawKbSprite(x, y, w, h, hex ? kb_hextog : kb_dec, KB_TOG_W, KB_TOG_H, 0);
                else if (c == 23)  // OK sprite
                    DrawKbSprite(x, y, w, h, kb_ok, KB_OK_W, KB_OK_H, 0);
                else               // Cancel sprite
                    DrawKbSprite(x, y, w, h, kb_cancel, KB_CANCEL_W, KB_CANCEL_H, 0);
                if (hot && c != 22) CFillBlend(x, y, w, h, 255, 255, 255, 45); // press flash (not the toggle: swapping DEC/HEX is feedback enough, and the flash would persist)
            }
            BotBlitComposeBoth();
            changed = 0;
        }

        svcSleepThread(16 * 1000 * 1000);
        if (HID_PAD & BUTTON_B) { *cancel = 1; return initial; } // physical B cancels

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;

        for (int i = 0; i < nb; ++i)
        {
            if (!KbHit(px, py, b[i].x, b[i].y, b[i].w, b[i].h)) continue;
            int c = b[i].code;
            if (c <= 15)
            {
                if (c >= 10 && !hex) break;             // hex digit disabled in dec
                u32 base = hex ? 16 : 10, nv = val * base + c;
                if (nv >= val) val = nv;                 // ignore overflow
                hotCode = c;
            }
            else if (c == 20) { val = hex ? (val >> 4) : (val / 10); hotCode = c; }
            else if (c == 21) { val = 0; hotCode = c; }
            else if (c == 22) { hex = !hex; hotCode = c; }
            else if (c == 23) return val;
            else { *cancel = 1; return initial; }
            changed = 1;
            break;
        }
    }
}

// ---- memory region walk (game's own RW private regions) ----
// Region clamp for the walk (set from the Cheat Search "Memory Region" preset).
static u32 g_scanLo, g_scanHi;
typedef void (*RegionCb)(u32 base, u32 size, void *ud);
static void ForEachRWRegion(RegionCb cb, void *ud)
{
    u32 lo = g_scanLo ? g_scanLo : 0x00100000;
    u32 hi = g_scanHi ? g_scanHi : 0x40000000;
    u32 addr = lo;
    while (addr < hi)
    {
        MemInfo info; PageInfo pg;
        if (R_FAILED(svcQueryMemory(&info, &pg, addr))) break;
        u32 next = info.base_addr + info.size;
        if (info.size == 0) break;
        // any readable+writable region (skip free/IO/reserved). This covers the
        // game's .data/.bss and heaps, whatever their exact MemState. Read-only
        // shared blocks (font, hid shmem) are excluded by the WRITE requirement.
        if ((info.perm & MEMPERM_READ) && (info.perm & MEMPERM_WRITE) &&
            info.state != MEMSTATE_FREE && info.state != MEMSTATE_IO &&
            info.state != MEMSTATE_RESERVED)
        {
            u32 b = info.base_addr, e = info.base_addr + info.size;
            if (b < lo) b = lo;   // clamp to the selected region window
            if (e > hi) e = hi;
            if (e > b) cb(b, e - b, ud);
        }
        if (next <= addr) break;
        addr = next;
    }
}

static u32 ReadN(u32 a, int w)
{
    if (w == 1) return R8(a);
    if (w == 2) return R16(a);
    return R32(a);
}
static void WriteN(u32 a, int w, u32 v)
{
    if (w == 1) W8(a, (u8)v);
    else if (w == 2) W16(a, (u16)v);
    else W32(a, v);
}

// ---- Cheat Search (CTRPF-style: results table on top, form on bottom) ----
typedef struct { u32 addr, newv, oldv; } Cand;
static Cand *g_cands;
static u32   g_candCap, g_candCount;
static int   g_searchStarted, g_searchWidth = 4; // bytes: 1/2/4
static int   g_scanType = 0;   // 0 =  1 >  2 <  3 changed  4 unchanged  5 increased  6 decreased
static int   g_step, g_capped;
static u32   g_searchValue;
static u32   g_searchSelAddr;  // address under the cursor (shared with RAM Dumper)
static int   g_searchType = 0; // 0 = Known Value, 1 = Unknown Search
static int   g_memRegion  = 0; // region preset index

// Unknown Search: raw snapshot of the scanned region(s). We compare live memory
// against this later, so a value we never knew can still be tracked by how it
// changes. Bytes (not per-address candidates) keeps it compact enough to fit.
#define SNAP_CAP (2u * 1024 * 1024)
static u8   *g_snap;             // lazily allocated, persists for the session
static u32   g_snapUsed;
typedef struct { u32 base, size, off; } SnapReg;
static SnapReg g_snapReg[64];
static int   g_snapRegN;
static int   g_unknownArmed;     // snapshot taken, awaiting first comparison scan

// One-step Undo: backup of the candidate list before the last scan.
static Cand *g_undo;
static u32   g_undoCount;
static int   g_undoStep, g_undoArmed, g_undoStarted, g_undoValid;

static const char *SCAN_NAME[7] = {
    "Equal To", "Greater Than", "Less Than", "Changed", "Unchanged", "Increased", "Decreased"
};
static const char *SEARCHTYPE_NAME[2] = { "Known Value", "Unknown Search" };
#define NUM_REGIONS 4
static const char *REGION_NAME[NUM_REGIONS] = {
    "All Memory", "Low 1-128M", "Mid 128-512M", "High 512M-1G"
};
static void RegionBounds(int r, u32 *lo, u32 *hi)
{
    switch (r) {
        case 1: *lo = 0x00100000; *hi = 0x08000000; break;
        case 2: *lo = 0x08000000; *hi = 0x20000000; break;
        case 3: *lo = 0x20000000; *hi = 0x40000000; break;
        default:*lo = 0x00100000; *hi = 0x40000000; break;
    }
}
static int ScanNeedsValue(int st) { return st <= 2; } // 0/1/2 use the value field

static u32 g_dbgRegions, g_dbgKB; // diagnostics: what the last scan visited

typedef struct { u32 value; } SeedCtx;
static void SeedCb(u32 base, u32 size, void *ud)
{
    g_dbgRegions++; g_dbgKB += size / 1024;
    u32 v = ((SeedCtx *)ud)->value; int w = g_searchWidth;
    for (u32 a = base; a + w <= base + size; a += w)
    {
        if (ReadN(a, w) != v) continue;
        if (g_candCount >= g_candCap) { g_capped = 1; return; }
        g_cands[g_candCount].addr = a;
        g_cands[g_candCount].newv = v;
        g_cands[g_candCount].oldv = v;
        g_candCount++;
    }
}
static void SearchSeed(u32 v)
{
    g_candCount = 0; g_capped = 0; g_dbgRegions = 0; g_dbgKB = 0;
    SeedCtx c = { v };
    ForEachRWRegion(SeedCb, &c);
    g_searchStarted = 1; g_step = 1;
}
static int MatchScan(int st, u32 nv, u32 ov, u32 val)
{
    switch (st)
    {
        case 0: return nv == val;  case 1: return nv > val;  case 2: return nv < val;
        case 3: return nv != ov;   case 4: return nv == ov;
        case 5: return nv > ov;    default: return nv < ov;
    }
}
static void SearchNext(u32 val)
{
    int w = g_searchWidth; u32 keep = 0;
    for (u32 i = 0; i < g_candCount; ++i)
    {
        u32 nv = ReadN(g_cands[i].addr, w), ov = g_cands[i].newv;
        if (MatchScan(g_scanType, nv, ov, val))
        {
            g_cands[keep].addr = g_cands[i].addr;
            g_cands[keep].newv = nv; g_cands[keep].oldv = ov; keep++;
        }
    }
    g_candCount = keep; g_step++;
}

// ---- Unknown Search: raw snapshot, compared on the next scan ----
static void SnapCb(u32 base, u32 size, void *ud)
{
    (void)ud;
    if (!g_snap || g_snapRegN >= 64 || g_snapUsed >= SNAP_CAP) { g_capped = 1; return; }
    u32 room = SNAP_CAP - g_snapUsed;
    u32 n = size; if (n > room) { n = room; g_capped = 1; }
    memcpy(g_snap + g_snapUsed, (const void *)base, n);
    g_snapReg[g_snapRegN].base = base;
    g_snapReg[g_snapRegN].size = n;
    g_snapReg[g_snapRegN].off  = g_snapUsed;
    g_snapRegN++;
    g_snapUsed += n;
}
static void SnapshotArm(void)
{
    g_candCount = 0; g_capped = 0; g_snapUsed = 0; g_snapRegN = 0;
    if (!g_snap) { QueueToastRaw("No snapshot buffer", ""); return; }
    ForEachRWRegion(SnapCb, NULL);
    g_searchStarted = 1; g_unknownArmed = 1; g_step = 1;
}
static u32 SnapN(u32 off, int w)
{
    u8 *p = g_snap + off;
    if (w == 1) return p[0];
    if (w == 2) return (u32)p[0] | ((u32)p[1] << 8);
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
// First comparison after a snapshot: build candidates where live memory relates
// to the snapshot the way the scan type says (Changed / Increased / Decreased...).
static void SnapshotMaterialize(u32 val)
{
    int w = g_searchWidth; g_candCount = 0; g_capped = 0;
    for (int r = 0; r < g_snapRegN; ++r)
    {
        u32 base = g_snapReg[r].base, sz = g_snapReg[r].size, off = g_snapReg[r].off;
        for (u32 a = base; a + w <= base + sz; a += w)
        {
            u32 nv = ReadN(a, w);
            u32 ov = SnapN(off + (a - base), w);
            if (!MatchScan(g_scanType, nv, ov, val)) continue;
            if (g_candCount >= g_candCap) { g_capped = 1; break; }
            g_cands[g_candCount].addr = a;
            g_cands[g_candCount].newv = nv;
            g_cands[g_candCount].oldv = ov;
            g_candCount++;
        }
        if (g_capped) break;
    }
    g_unknownArmed = 0; g_step++;
}

// ---- one-step Undo ----
static void SaveUndo(void)
{
    if (!g_undo) return;
    memcpy(g_undo, g_cands, g_candCount * sizeof(Cand));
    g_undoCount = g_candCount; g_undoStep = g_step;
    g_undoArmed = g_unknownArmed; g_undoStarted = g_searchStarted;
    g_undoValid = 1;
}
static void DoUndo(void)
{
    if (!g_undoValid || !g_undo) { QueueToastRaw("Nothing to undo", ""); return; }
    memcpy(g_cands, g_undo, g_undoCount * sizeof(Cand));
    g_candCount = g_undoCount; g_step = g_undoStep;
    g_unknownArmed = g_undoArmed; g_searchStarted = g_undoStarted;
    g_undoValid = 0; g_capped = 0;
    QueueToastRaw("Undo", ": last scan reverted");
}

// perform a search step using the current form state
static void DoSearch(void)
{
    if (!g_candCap) { QueueToastRaw("No memory", ""); return; }
    RegionBounds(g_memRegion, &g_scanLo, &g_scanHi); // apply the region window
    SaveUndo();
    if (!g_searchStarted)
    {
        if (g_searchType == 1) SnapshotArm();            // Unknown: capture snapshot
        else                   SearchSeed(g_searchValue);// Known: seed by value
    }
    else if (g_unknownArmed) SnapshotMaterialize(g_searchValue); // first compare vs snapshot
    else                     SearchNext(g_searchValue);          // subsequent filters
}

// --- top screen: results table (Address / New / Old) with a selection cursor ---
#define SR_ROWS 9   // visible result rows
static void SearchDrawResults(int scroll, int cursor)
{
    ComposeBackdrop();
    CText(WIN_X + 12, WIN_Y + 6, T("Cheat Search"), GOLD, 1);
    char hit[48];
    if (!g_searchStarted) sniprintf(hit, sizeof hit, "%s", T("no search"));
    else if (g_unknownArmed) sniprintf(hit, sizeof hit, "Snapshot %luKB%s", (unsigned long)(g_snapUsed / 1024), g_capped ? "+" : "");
    else sniprintf(hit, sizeof hit, "Step %d   Hits: %lu%s", g_step, (unsigned long)g_candCount, g_capped ? "+" : "");
    CText6(WIN_X + WIN_W - 12 - C6Width(hit), WIN_Y + 9, hit, g_capped ? 233 : 196, g_capped ? 115 : 180, g_capped ? 107 : 150);
    CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);

    int cx0 = WIN_X + 16, cx1 = WIN_X + 140, cx2 = WIN_X + 244;
    CText6(cx0, WIN_Y + 26, T("Address"), INK_DIM);
    CText6(cx1, WIN_Y + 26, T("New Value"), INK_DIM);
    CText6(cx2, WIN_Y + 26, T("Old Value"), INK_DIM);

    int rowY = WIN_Y + 40, rh = 15, w = g_searchWidth;
    for (int i = scroll; i < (int)g_candCount && i < scroll + SR_ROWS; ++i)
    {
        int y = rowY + (i - scroll) * rh;
        if (i == cursor)
        {
            CFillBlend(WIN_X + 12, y - 1, WIN_W - 24, rh, 0, 0, 0, 120);
            CFill(WIN_X + 12, y - 1, 2, rh, GOLD);
        }
        char a[12], nv[14], ov[14];
        siprintf(a, "%08lX", (unsigned long)g_cands[i].addr);
        siprintf(nv, "%lu", (unsigned long)ReadN(g_cands[i].addr, w)); // New = live value
        siprintf(ov, "%lu", (unsigned long)g_cands[i].newv);           // Old = value at last scan (the Next baseline)
        CText6(cx0, y, a, (i == cursor) ? 246 : 236, (i == cursor) ? 236 : 224, (i == cursor) ? 200 : 198);
        CText6(cx1, y, nv, GREEN_ON);
        CText6(cx2, y, ov, INK_DIM);
    }
    if (!g_candCount)
    {
        if (g_unknownArmed)
        {
            CText6(cx0, rowY,      T("Snapshot taken."), GREEN_ON);
            CText6(cx0, rowY + 16, T("1. Change the value in the game."), INK_DIM);
            CText6(cx0, rowY + 29, T("2. Pick Changed / Decreased / etc."), INK_DIM);
            CText6(cx0, rowY + 42, T("3. Search. Repeat to narrow down."), INK_DIM);
        }
        else
            CText6(cx0, rowY, g_searchStarted ? T("(no matches)") : T("Set the form below, then Search."), INK_DIM);
    }

    // scroll arrows (right edge)
    if (scroll > 0)
        for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, rowY + 2 + a, 1 + 2 * a, 1, GOLD);
    if (scroll + SR_ROWS < (int)g_candCount)
        for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, rowY + SR_ROWS * rh - 4 - a, 1 + 2 * a, 1, GOLD);

    // footer: controls (left) + page indicator (right), guarded against overlap
    if (g_candCount)
    {
        const char *leg = T("{A} poke  {DP} move  {B} exit");
        CText6(WIN_X + 12, WIN_Y + WIN_H - 14, leg, INK_DIM);
        char pg[28];
        siprintf(pg, "%d / %lu", cursor + 1, (unsigned long)g_candCount);
        int pgX = WIN_X + WIN_W - 12 - C6Width(pg);
        int legEnd = WIN_X + 12 + C6Width(leg);
        if (pgX > legEnd + 8) // only draw if it clears the legend
            CText6(pgX, WIN_Y + WIN_H - 14, pg, INK_DIM);
    }
    else
        CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{X} type  {Y} scan  {R} search  {B} exit"), INK_DIM);
}

// --- bottom screen: touch form ---
// codes: 1 valtype  2 scan  3 value  4 search  5 reset  6 region  7 searchtype  8 undo
typedef struct { int x, y, w, h, code; } FBox;
#define SF_VX  120
#define SF_VW  186
#define SF_FY  36
#define SF_FH  21
#define SF_G   4
static int SearchBuildForm(FBox *f)
{
    int n = 0;
    static const int codes[5] = { 6, 7, 1, 2, 3 }; // region, search type, value type, scan, value
    for (int i = 0; i < 5; ++i)
        f[n++] = (FBox){ SF_VX, SF_FY + i * (SF_FH + SF_G), SF_VW, SF_FH, codes[i] };
    int by = SF_FY + 5 * (SF_FH + SF_G) + 8;
    f[n++] = (FBox){ 14,  by, 93, 28, 4 };  // Search
    f[n++] = (FBox){ 113, by, 93, 28, 8 };  // Undo
    f[n++] = (FBox){ 212, by, 94, 28, 5 };  // Reset
    return n;
}
static void SearchDrawForm(void)
{
    // raw frozen game frame...
    for (int y = 0; y < BOT_H; ++y)
        for (int x = 0; x < BOT_W; ++x)
        {
            u8 *p = CPix(x, y);
            if (savedBotValid)
            { u16 v = savedBot[y * BOT_W + x];
              p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
            else { p[0] = p[1] = p[2] = 12; }
        }
    // ...covered by a solid dark-brown panel so fields are always readable
    CFillBlend(0, 0, BOT_W, BOT_H, BG, 230); // ~80% opaque
    CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);

    CText(14, 8, T("Cheat Search"), GOLD, 1);
    CFill(14, 30, C6Width(T("Cheat Search")) * 2, 1, GOLD);

    int lx = 14, vx = SF_VX, vw = SF_VW, fy = SF_FY, fh = SF_FH, g = SF_G;
    const char *labels[5] = { "Memory Region", "Search Type", "Value Type", "Scan Type", "Value" };
    int locked = g_searchStarted; // region / type / width are fixed once a search starts
    int dimValue = (!ScanNeedsValue(g_scanType)) || (g_searchType == 1 && !g_searchStarted);
    int dims[5] = { locked, locked, locked, 0, dimValue };
    char val[5][40];
    sniprintf(val[0], sizeof val[0], "%s", T(REGION_NAME[g_memRegion]));
    sniprintf(val[1], sizeof val[1], "%s", T(SEARCHTYPE_NAME[g_searchType]));
    siprintf(val[2], "%d Bytes  (%d-bit)", g_searchWidth, g_searchWidth * 8);
    sniprintf(val[3], sizeof val[3], "%s", T(SCAN_NAME[g_scanType]));
    if (g_searchType == 1 && !g_searchStarted) sniprintf(val[4], sizeof val[4], "%s", T("(not needed)"));
    else if (dimValue)                         siprintf(val[4], "--");
    else siprintf(val[4], "%lu  (0x%lX)", (unsigned long)g_searchValue, (unsigned long)g_searchValue);

    for (int i = 0; i < 5; ++i)
    {
        int y = fy + i * (fh + g);
        CText6(lx, y + 4, T(labels[i]), INK);
        int dim = dims[i];
        CFill(vx, y, vw, fh, dim ? 26 : 52, dim ? 20 : 40, dim ? 12 : 22);
        CFill(vx, y, vw, 1, GOLD); CFill(vx, y + fh - 1, vw, 1, GOLD);
        CFill(vx, y, 1, fh, GOLD); CFill(vx + vw - 1, y, 1, fh, GOLD);
        CText6(vx + 6, y + 4, val[i], dim ? 120 : 236, dim ? 116 : 236, dim ? 96 : 210);
    }
    int by = fy + 5 * (fh + g) + 8;
    KbKey(14,  by, 93, 28, g_searchStarted ? T("Next") : T("Search"), 5, 0);
    KbKey(113, by, 93, 28, T("Undo"), g_undoValid ? 4 : 3, 0);
    KbKey(212, by, 94, 28, T("Reset"), 6, 0);
    const char *lg1 = T("Tap a field, or:");
    const char *lg2 = T("{X} type   {Y} scan   {R} search   {L} undo");
    CText6((BOT_W - C6Width(lg1)) / 2, by + 31, lg1, INK_DIM);
    CText6Btn((BOT_W - C6BtnWidth(lg2)) / 2, by + 43, lg2, INK_DIM);
    BotBlitComposeBoth();
}

static void ToolSearch(void)
{
    KbInit(); // touch input for the form (was missing -> taps did nothing)
    if (!g_cands)
    {
        g_candCap = 0x8000; // 32768 * 12B = 384KB
        g_cands = (Cand *)malloc(g_candCap * sizeof(Cand));
        if (!g_cands) g_candCap = 0;
    }
    if (!g_undo && g_candCap)                       // one-step undo backup (384KB)
        g_undo = (Cand *)malloc(g_candCap * sizeof(Cand));
    if (!g_snap)                                    // Unknown Search snapshot (2MB)
        g_snap = (u8 *)malloc(SNAP_CAP);

    int scroll = 0, cursor = 0, redrawTop = 1, redrawBot = 1;
    u32 prev = HID_PAD;
    int touchPrev = HidTouch(0, 0);

    while (1)
    {
        if (cursor >= (int)g_candCount) cursor = g_candCount ? g_candCount - 1 : 0;
        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + SR_ROWS) scroll = cursor - SR_ROWS + 1;
        if (g_candCount) g_searchSelAddr = g_cands[cursor].addr; // shared with RAM Dumper

        if (redrawTop) { SearchDrawResults(scroll, cursor); Present(); Present(); redrawTop = 0; }
        if (redrawBot) { SearchDrawForm(); redrawBot = 0; }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if ((down & BUTTON_DOWN) && cursor + 1 < (int)g_candCount) { cursor++; redrawTop = 1; }
        if ((down & BUTTON_UP)   && cursor > 0)                    { cursor--; redrawTop = 1; }
        if (down & BUTTON_B) break;
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; } // straight to game, results kept

        // physical shortcuts: X = value type, Y = scan type, R = Search
        if (down & BUTTON_X)
        {
            if (!g_searchStarted) g_searchWidth = (g_searchWidth == 1) ? 2 : (g_searchWidth == 2) ? 4 : 1;
            else QueueToastRaw("Reset to change width", "");
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_Y)
        {
            g_scanType = (g_scanType + 1) % 7;
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_L1) // L = undo last scan
        {
            DoUndo(); scroll = 0; cursor = 0;
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_R1)
        {
            ComposeBackdrop(); CText(WIN_X + 110, WIN_Y + 90, T("Scanning..."), GOLD, 1); Present(); Present();
            DoSearch(); scroll = 0; cursor = 0;
            redrawTop = 1; redrawBot = 1;
            prev = HID_PAD;
            continue;
        }

        // A = poke the selected result
        if ((down & BUTTON_A) && g_candCount)
        {
            int cancel;
            u32 addr = g_cands[cursor].addr;
            u32 nv = EnterNum("Poke value", ReadN(addr, g_searchWidth), &cancel);
            if (!cancel) { WriteN(addr, g_searchWidth, nv); g_cands[cursor].newv = nv; QueueToastRaw("Poked", ""); }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            continue;
        }

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;
        FBox f[8]; int nf = SearchBuildForm(f);
        for (int i = 0; i < nf; ++i)
        {
            if (!KbHit(px, py, f[i].x, f[i].y, f[i].w, f[i].h)) continue;
            switch (f[i].code)
            {
                case 1: // value type (only before first search)
                    if (!g_searchStarted)
                        g_searchWidth = (g_searchWidth == 1) ? 2 : (g_searchWidth == 2) ? 4 : 1;
                    else QueueToastRaw("Reset to change width", "");
                    break;
                case 2: g_scanType = (g_scanType + 1) % 7; break;
                case 3: // value entry
                {
                    int cancel;
                    u32 v = EnterNum("Search value", g_searchValue, &cancel);
                    if (!cancel) g_searchValue = v;
                    break;
                }
                case 4: // Search
                    ComposeBackdrop(); CText(WIN_X + 110, WIN_Y + 90, T("Scanning..."), GOLD, 1); Present(); Present();
                    DoSearch(); scroll = 0; cursor = 0;
                    break;
                case 5: // Reset (clears results, snapshot and undo; keeps your form choices)
                    g_candCount = 0; g_searchStarted = 0; g_capped = 0; g_step = 0;
                    g_unknownArmed = 0; g_snapUsed = 0; g_snapRegN = 0; g_undoValid = 0;
                    scroll = 0; cursor = 0;
                    break;
                case 6: // memory region (only before a search starts)
                    if (!g_searchStarted) g_memRegion = (g_memRegion + 1) % NUM_REGIONS;
                    else QueueToastRaw("Reset to change region", "");
                    break;
                case 7: // search type: Known / Unknown (only before a search starts)
                    if (!g_searchStarted) g_searchType = (g_searchType + 1) % 2;
                    else QueueToastRaw("Reset to change type", "");
                    break;
                case 8: // Undo
                    DoUndo(); scroll = 0; cursor = 0;
                    break;
            }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            break;
        }
    }

    // Search state PERSISTS across tool exits and full menu close/reopen.
    // This is what enables the real cheat-search loop: seed a value -> exit menu
    // (unpauses the game) -> change the value in-game -> reopen -> filter by
    // Changed / Increased / Decreased / equals. The g_cands buffer stays
    // allocated for the plugin lifetime; only the "Reset" button clears results.
}

// ---- About (scrollable credits) ----
static void ToolAbout(void)
{
    static const struct { const char *s; int gold; } lines[] = {
        { "OcarinaCTRComposer  " PLUGIN_VER,            1 },
        { "",                                           0 },
        { "Made by samabr",                             1 },
        { "github.com/samaBR85/OcarinaCTRComposer",     0 },
        { "",                                           0 },
        { "Cheats & codes: Nanquitas / Fort42",         0 },
        { "Item icons: Spriters Resource",              0 },
        { "   (ripped by Colbydude)",                   0 },
        { "Buttons/logo: Spriters Resource (manpaint)", 0 },
        { "Loader: Luma3DS (LumaTeam)",                 0 },
        { "3GX loader / tool: PabloMK7",                0 },
        { "Walkthrough: z64central.com",                0 },
        { "   (packaged by LowEndC)",                   0 },
        { "Save-data map: HelpTheWretched",             0 },
        { "Teleport refs: gamestabled &",               0 },
        { "   HylianFreddy (practice menus)",           0 },
        { "",                                           0 },
        { "The Legend of Zelda (c) Nintendo.",          0 },
        { "Fan project, non-commercial.",               0 },
    };
    int N = (int)(sizeof(lines) / sizeof(lines[0]));
    int x = WIN_X + 16;
    int top = WIN_Y + 10 + LOGO_H + 10;   // text block starts below the fixed logo
    int footY = WIN_Y + WIN_H - 16;
    int vis = (footY - top - 4) / 12;      // lines that fit in the scroll area
    int scroll = 0, redraw = 1;
    u32 prev = HID_PAD;
    while (1)
    {
        if (redraw)
        {
            ComposeBackdrop();
            DrawImg(WIN_X + (WIN_W - LOGO_W) / 2, WIN_Y + 10, logoPx, LOGO_W, LOGO_H);
            for (int i = 0; i < vis && scroll + i < N; ++i)
            {
                const char *s = lines[scroll + i].s;
                if (s[0]) CText6(x, top + i * 12, s, RGB3(lines[scroll + i].gold ? CGOLD : CDIM));
            }
            if (scroll > 0)                          // up arrow (more above)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, top + 3 + a, 1 + 2*a, 1, GOLD);
            if (scroll + vis < N)                    // down arrow (more below)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 16 - a, footY - 6 - a, 1 + 2*a, 1, GOLD);
            CText6Btn(x, footY, "{DP} scroll    {B} back", INK_DIM);
            Present(); Present();
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if ((down & BUTTON_DOWN) && scroll + vis < N) { scroll++; redraw = 1; }
        if ((down & BUTTON_UP)   && scroll > 0)       { scroll--; redraw = 1; }
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; }
        if (down & (BUTTON_B | BUTTON_A)) break;
    }
    DrainButtons(~0u); // capped: a stuck pad must not hang the console with the game paused
}

// ---- RAM Dumper ----
static u32 g_dumpStart = 0x08000000;
static int g_dumpSizeIdx = 3;
static const u32   DUMP_SIZES[6]   = { 0x1000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000 };
static const char *DUMP_SIZE_NM[6] = { "4 KB", "64 KB", "256 KB", "1 MB", "4 MB", "16 MB" };
#define NUM_DUMP_SIZES 6
#define DUMP_DIR "/luma/plugins/0004000000033500/dumps"

// How many contiguous readable bytes exist from `start`, so a dump never faults
// on an unmapped hole. Returns bytes readable (<= want), or 0 if start is dead.
static u32 ReadableSpan(u32 start, u32 want)
{
    u32 end = start + want, a = start;
    while (a < end)
    {
        MemInfo info; PageInfo pg;
        if (R_FAILED(svcQueryMemory(&info, &pg, a))) break;
        u32 rend = info.base_addr + info.size;
        if (!(info.perm & MEMPERM_READ) || info.state == MEMSTATE_FREE) break;
        if (rend >= end) return want;
        if (rend <= a) break;
        a = rend;
    }
    return a > start ? a - start : 0;
}

static void RamDumpDrawTop(const char *status, u8 sr, u8 sg, u8 sb, int pct)
{
    ComposeBackdrop();
    CText(WIN_X + 12, WIN_Y + 6, T("RAM Dumper"), GOLD, 1);
    CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);
    u32 size = DUMP_SIZES[g_dumpSizeIdx];
    int x = WIN_X + 16, y = WIN_Y + 30; char l[72];
    sniprintf(l, sizeof l, "%s  0x%08lX", T("Start:"), (unsigned long)g_dumpStart);          CText6(x, y, l, INK); y += 15;
    sniprintf(l, sizeof l, "%s  0x%08lX", T("End:"), (unsigned long)(g_dumpStart + size)); CText6(x, y, l, INK); y += 15;
    sniprintf(l, sizeof l, "%s  %s", T("Size:"), DUMP_SIZE_NM[g_dumpSizeIdx]);              CText6(x, y, l, INK); y += 19;
    CText6(x, y, T("Saves to:"), INK_DIM); y += 13;
    CText6(x, y, DUMP_DIR "/", INK_DIM); y += 13;
    CText6(x, y, "  dump_<start>_<size>.bin", INK_DIM); y += 19;
    if (status) CText6(x, y, T(status), sr, sg, sb);
    y += 16;
    if (pct >= 0)
    {
        int bw = WIN_W - 40, bx = x;
        CFill(bx, y, bw, 9, 40, 32, 20);
        CFill(bx, y, bw * pct / 100, 9, 120, 200, 120);
        CFill(bx, y, bw, 1, GOLD); CFill(bx, y + 8, bw, 1, GOLD);
        CFill(bx, y, 1, 9, GOLD);  CFill(bx + bw - 1, y, 1, 9, GOLD);
    }
    CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{X} size  {Y} from-search  {R} dump  {B} exit"), INK_DIM);
    Present(); Present();
}

static void RamDumpDrawForm(void)
{
    for (int yy = 0; yy < BOT_H; ++yy)
        for (int xx = 0; xx < BOT_W; ++xx)
        {
            u8 *p = CPix(xx, yy);
            if (savedBotValid)
            { u16 v = savedBot[yy * BOT_W + xx];
              p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
            else { p[0] = p[1] = p[2] = 12; }
        }
    CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
    CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
    CText(14, 8, T("RAM Dumper"), GOLD, 1);
    CFill(14, 30, C6Width(T("RAM Dumper")) * 2, 1, GOLD);

    int lx = 14, vx = 120, vw = 186, fy = 66, fh = 26, g = 12; // fy centers the form block in the lower area
    const char *labels[2] = { "Start Addr", "Size" };
    char val[2][40];
    siprintf(val[0], "0x%08lX", (unsigned long)g_dumpStart);
    sniprintf(val[1], sizeof val[1], "%s", DUMP_SIZE_NM[g_dumpSizeIdx]);
    for (int i = 0; i < 2; ++i)
    {
        int y = fy + i * (fh + g);
        CText6(lx, y + 6, T(labels[i]), INK);
        CFill(vx, y, vw, fh, 52, 40, 22);
        CFill(vx, y, vw, 1, GOLD); CFill(vx, y + fh - 1, vw, 1, GOLD);
        CFill(vx, y, 1, fh, GOLD); CFill(vx + vw - 1, y, 1, fh, GOLD);
        CText6(vx + 6, y + 6, val[i], 236, 236, 210);
    }
    int by = fy + 2 * (fh + g) + 10;
    KbKey(14,  by, 140, 32, T("From Search"), 4, 0);
    KbKey(166, by, 140, 32, T("Dump"), 5, 0);
    const char *hint = T("Tap a field, or use the buttons");
    CText6((BOT_W - C6Width(hint)) / 2, by + 38, hint, INK_DIM);
    BotBlitComposeBoth();
}

typedef struct { int x, y, w, h, code; } FBox2;
static int RamDumpBuildForm(FBox2 *f)
{
    int n = 0, vx = 120, vw = 186, fy = 66, fh = 26, g = 12; // fy must match ComposeRamDump above
    f[n++] = (FBox2){ vx, fy + 0 * (fh + g), vw, fh, 1 }; // start
    f[n++] = (FBox2){ vx, fy + 1 * (fh + g), vw, fh, 2 }; // size
    int by = fy + 2 * (fh + g) + 10;
    f[n++] = (FBox2){ 14,  by, 140, 32, 3 };              // From Search
    f[n++] = (FBox2){ 166, by, 140, 32, 4 };              // Dump
    return n;
}

static void DoDump(const char **status, u8 *sr, u8 *sg, u8 *sb)
{
    u32 want = DUMP_SIZES[g_dumpSizeIdx];
    u32 span = ReadableSpan(g_dumpStart, want);
    if (!span)      { *status = "Address not readable - try another Start"; *sr=236; *sg=140; *sb=120; return; }
    FsBootInit();
    if (!fsReady)   { *status = "SD card not available"; *sr=236; *sg=140; *sb=120; return; }
    FSUSER_CreateDirectory(cfgArchive, fsMakePath(PATH_ASCII, DUMP_DIR), 0); // ok if it exists
    char path[100];
    sniprintf(path, sizeof path, "%s/dump_%08lX_%luK.bin", DUMP_DIR,
             (unsigned long)g_dumpStart, (unsigned long)(span / 1024));
    Handle fh;
    if (R_FAILED(FSUSER_OpenFile(&fh, cfgArchive, fsMakePath(PATH_ASCII, path),
                                 FS_OPEN_WRITE | FS_OPEN_CREATE, 0)))
    { *status = "Could not create file"; *sr=236; *sg=140; *sb=120; return; }
    FSFILE_SetSize(fh, span);
    u32 off = 0, chunk = 0x40000; Result r = 0; int lastPct = -1;
    while (off < span)
    {
        u32 n = span - off; if (n > chunk) n = chunk;
        u32 wrote = 0;
        r = FSFILE_Write(fh, &wrote, off, (const void *)(g_dumpStart + off), n, 0);
        if (R_FAILED(r)) break;
        off += n;
        int pct = (int)((u64)off * 100 / span);
        if (pct != lastPct) { lastPct = pct; RamDumpDrawTop("Dumping...", 236, 200, 120, pct); }
    }
    FSFILE_Close(fh);
    if (R_FAILED(r)) { *status = "Write error"; *sr=236; *sg=140; *sb=120; return; }
    static char msg[80];
    if (span < want) siprintf(msg, "Saved %luKB (clamped to readable memory)", (unsigned long)(span / 1024));
    else             siprintf(msg, "Saved %luKB to dumps/dump_%08lX_%luK.bin",
                              (unsigned long)(span / 1024), (unsigned long)g_dumpStart, (unsigned long)(span / 1024));
    *status = msg; *sr=140; *sg=236; *sb=120;
    QueueToastRaw("RAM dumped", "");
}

static void ToolRamDump(void)
{
    KbInit();
    if (g_dumpStart == 0x08000000 && g_searchSelAddr) g_dumpStart = g_searchSelAddr; // handy default
    int redrawTop = 1, redrawBot = 1;
    u32 prev = HID_PAD; int touchPrev = HidTouch(0, 0);
    const char *status = NULL; u8 sr = 140, sg = 236, sb = 120;

    while (1)
    {
        if (redrawTop) { RamDumpDrawTop(status, sr, sg, sb, -1); redrawTop = 0; }
        if (redrawBot) { RamDumpDrawForm(); redrawBot = 0; }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if (down & BUTTON_B) break;
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; }
        if (down & BUTTON_X) { g_dumpSizeIdx = (g_dumpSizeIdx + 1) % NUM_DUMP_SIZES; redrawTop = 1; redrawBot = 1; }
        if (down & BUTTON_Y)
        {
            if (g_searchSelAddr) { g_dumpStart = g_searchSelAddr; status = "Loaded address from Cheat Search"; sr=236; sg=200; sb=120; }
            else                 { status = "No Cheat Search result yet"; sr=236; sg=180; sb=120; }
            redrawTop = 1; redrawBot = 1;
        }
        if (down & BUTTON_R1) { DoDump(&status, &sr, &sg, &sb); redrawTop = 1; redrawBot = 1; prev = HID_PAD; continue; }

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;
        FBox2 f[6]; int nf = RamDumpBuildForm(f);
        for (int i = 0; i < nf; ++i)
        {
            if (!KbHit(px, py, f[i].x, f[i].y, f[i].w, f[i].h)) continue;
            switch (f[i].code)
            {
                case 1: { int c; u32 v = EnterNum("Start address", g_dumpStart, &c); if (!c) g_dumpStart = v; } break;
                case 2: g_dumpSizeIdx = (g_dumpSizeIdx + 1) % NUM_DUMP_SIZES; break;
                case 3:
                    if (g_searchSelAddr) { g_dumpStart = g_searchSelAddr; status = "Loaded address from Cheat Search"; sr=236; sg=200; sb=120; }
                    else                 { status = "No Cheat Search result yet"; sr=236; sg=180; sb=120; }
                    break;
                case 4: DoDump(&status, &sr, &sg, &sb); break;
            }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            break;
        }
    }
}

// ---- Hex Editor ----
#define HEX_COLS 8
#define HEX_ROWS 11
#define HEX_LO   0x00100000u
#define HEX_HI   0x40000000u
static u32 g_hexCursor = 0x08000000;
static u32 g_hexView   = 0x08000000;

// cached readable window so a page of bytes costs ~1 svcQueryMemory, not 88.
static u32 g_mrLo = 1, g_mrHi = 0; static int g_mrOk = 0;
static int MemReadable(u32 a)
{
    if (a >= g_mrLo && a < g_mrHi) return g_mrOk;
    MemInfo info; PageInfo pg;
    if (R_FAILED(svcQueryMemory(&info, &pg, a))) { g_mrLo = a & ~0xFFFu; g_mrHi = g_mrLo + 0x1000; g_mrOk = 0; return 0; }
    g_mrLo = info.base_addr; g_mrHi = info.base_addr + info.size;
    g_mrOk = (info.perm & MEMPERM_READ) && info.state != MEMSTATE_FREE;
    return g_mrOk;
}
static int MemWritable(u32 a)
{
    MemInfo info; PageInfo pg;
    if (R_FAILED(svcQueryMemory(&info, &pg, a))) return 0;
    return (info.perm & MEMPERM_WRITE) && info.state != MEMSTATE_FREE;
}

static void HexClampCursor(void)
{
    if (g_hexCursor < HEX_LO) g_hexCursor = HEX_LO;
    if (g_hexCursor > HEX_HI - 1) g_hexCursor = HEX_HI - 1;
    u32 span = HEX_ROWS * HEX_COLS;
    u32 curRow = g_hexCursor - (g_hexCursor % HEX_COLS);
    if (g_hexCursor < g_hexView) g_hexView = curRow;
    else if (g_hexCursor >= g_hexView + span) g_hexView = curRow - (span - HEX_COLS);
    if (g_hexView < HEX_LO) g_hexView = HEX_LO;
}

static void HexDrawTop(void)
{
    ComposeBackdrop();
    CText(WIN_X + 12, WIN_Y + 6, T("Hex Editor"), GOLD, 1);
    char h[32];
    int rd = MemReadable(g_hexCursor);
    siprintf(h, "%08lX = %02X", (unsigned long)g_hexCursor, rd ? R8(g_hexCursor) : 0);
    CText6(WIN_X + WIN_W - 12 - C6Width(h), WIN_Y + 9, h, rd ? 236 : 200, rd ? 236 : 150, rd ? 210 : 120);
    CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);

    int ax = WIN_X + 12, hx0 = ax + 62, asc0 = hx0 + HEX_COLS * 20 + 6;
    for (int r = 0; r < HEX_ROWS; ++r)
    {
        u32 rowAddr = g_hexView + (u32)r * HEX_COLS;
        int y = WIN_Y + 28 + r * 13;
        char al[10]; siprintf(al, "%08lX", (unsigned long)rowAddr);
        CText6(ax, y, al, INK_DIM);
        for (int c = 0; c < HEX_COLS; ++c)
        {
            u32 a = rowAddr + c;
            int rdb = MemReadable(a);
            int bx = hx0 + c * 20;
            if (a == g_hexCursor)
            {
                CFillBlend(bx - 2, y - 1, 17, 12, 0, 0, 0, 150);
                CFill(bx - 2, y - 1, 17, 1, GOLD); CFill(bx - 2, y + 10, 17, 1, GOLD);
                CFill(bx - 2, y - 1, 1, 12, GOLD); CFill(bx + 14, y - 1, 1, 12, GOLD);
            }
            char bb[4]; if (rdb) siprintf(bb, "%02X", R8(a)); else siprintf(bb, "--");
            CText6(bx, y, bb, rdb ? 236 : 96, rdb ? 236 : 80, rdb ? 210 : 60);
            u8 v = rdb ? R8(a) : 0;
            char ch[2]; ch[0] = (v >= 32 && v < 127) ? (char)v : '.'; ch[1] = 0;
            CText6(asc0 + c * 7, y, ch, a == g_hexCursor ? 236 : 150, a == g_hexCursor ? 200 : 140, a == g_hexCursor ? 120 : 112);
        }
    }
    CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{DP} move  {A} edit  {X} goto  {B} exit"), INK_DIM);
    Present(); Present();
}

static void HexDrawForm(void)
{
    for (int yy = 0; yy < BOT_H; ++yy)
        for (int xx = 0; xx < BOT_W; ++xx)
        {
            u8 *p = CPix(xx, yy);
            if (savedBotValid)
            { u16 v = savedBot[yy * BOT_W + xx];
              p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
            else { p[0] = p[1] = p[2] = 12; }
        }
    CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
    CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
    CText(14, 8, T("Hex Editor"), GOLD, 1);
    CFill(14, 30, C6Width(T("Hex Editor")) * 2, 1, GOLD);

    int lx = 14, vx = 120, vw = 186, fy = 46, fh = 26, g = 12;
    const char *labels[2] = { "Address", "Byte @ cursor" };
    char val[2][40];
    siprintf(val[0], "0x%08lX", (unsigned long)g_hexCursor);
    if (MemReadable(g_hexCursor)) siprintf(val[1], "0x%02X  (%u)", R8(g_hexCursor), R8(g_hexCursor));
    else                          sniprintf(val[1], sizeof val[1], "%s", T("-- (unreadable)"));
    for (int i = 0; i < 2; ++i)
    {
        int y = fy + i * (fh + g);
        CText6(lx, y + 6, T(labels[i]), INK);
        CFill(vx, y, vw, fh, 52, 40, 22);
        CFill(vx, y, vw, 1, GOLD); CFill(vx, y + fh - 1, vw, 1, GOLD);
        CFill(vx, y, 1, fh, GOLD); CFill(vx + vw - 1, y, 1, fh, GOLD);
        CText6(vx + 6, y + 6, val[i], 236, 236, 210);
    }
    int by = fy + 2 * (fh + g) + 10;
    KbKey(14,  by, 140, 32, T("From Search"), 4, 0);
    KbKey(166, by, 140, 32, T("Edit Byte"), 5, 0);
    const char *hint = T("On top: {DP} move  {L}/{R} page  {Y} from-search");
    CText6Btn((BOT_W - C6BtnWidth(hint)) / 2, by + 38, hint, INK_DIM);
    BotBlitComposeBoth();
}

typedef struct { int x, y, w, h, code; } FBox3;
static int HexBuildForm(FBox3 *f)
{
    int n = 0, vx = 120, vw = 186, fy = 46, fh = 26, g = 12;
    f[n++] = (FBox3){ vx, fy + 0 * (fh + g), vw, fh, 1 }; // address (goto)
    f[n++] = (FBox3){ vx, fy + 1 * (fh + g), vw, fh, 2 }; // byte (edit)
    int by = fy + 2 * (fh + g) + 10;
    f[n++] = (FBox3){ 14,  by, 140, 32, 3 };              // From Search
    f[n++] = (FBox3){ 166, by, 140, 32, 4 };              // Edit Byte
    return n;
}

static void HexEditByte(void)
{
    if (!MemWritable(g_hexCursor)) { QueueToastRaw("Read-only here", ""); return; }
    int c; u32 v = EnterNum("Edit byte", MemReadable(g_hexCursor) ? R8(g_hexCursor) : 0, &c);
    if (!c) { W8(g_hexCursor, (u8)v); QueueToastRaw("Byte written", ""); }
}
static void HexGoto(void)
{
    int c; u32 a = EnterNum("Go to address", g_hexCursor, &c);
    if (!c) { g_hexCursor = a; HexClampCursor(); }
}

static void ToolHexEdit(void)
{
    KbInit();
    g_mrLo = 1; g_mrHi = 0; // reset readable cache
    if (g_searchSelAddr) g_hexCursor = g_searchSelAddr;   // start where the user was looking
    HexClampCursor(); g_hexView = g_hexCursor - (g_hexCursor % HEX_COLS);
    HexClampCursor();

    int redrawTop = 1, redrawBot = 1;
    u32 prev = HID_PAD; int touchPrev = HidTouch(0, 0);

    while (1)
    {
        if (redrawTop) { HexDrawTop(); redrawTop = 0; }
        if (redrawBot) { HexDrawForm(); redrawBot = 0; }

        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        if (down & BUTTON_B) break;
        if (down & BUTTON_SELECT) { g_quitToGame = 1; break; }

        int moved = 0;
        if (down & BUTTON_LEFT)  { g_hexCursor -= 1; moved = 1; }
        if (down & BUTTON_RIGHT) { g_hexCursor += 1; moved = 1; }
        if (down & BUTTON_UP)    { g_hexCursor -= HEX_COLS; moved = 1; }
        if (down & BUTTON_DOWN)  { g_hexCursor += HEX_COLS; moved = 1; }
        if (down & BUTTON_L1)    { g_hexCursor -= HEX_ROWS * HEX_COLS; moved = 1; }
        if (down & BUTTON_R1)    { g_hexCursor += HEX_ROWS * HEX_COLS; moved = 1; }
        if (moved) { HexClampCursor(); redrawTop = 1; redrawBot = 1; }

        if (down & BUTTON_A) { HexEditByte(); redrawTop = 1; redrawBot = 1; prev = HID_PAD; continue; }
        if (down & BUTTON_X) { HexGoto();     redrawTop = 1; redrawBot = 1; prev = HID_PAD; continue; }
        if (down & BUTTON_Y)
        {
            if (g_searchSelAddr) { g_hexCursor = g_searchSelAddr; HexClampCursor(); }
            else QueueToastRaw("No Cheat Search result yet", "");
            redrawTop = 1; redrawBot = 1;
        }

        int px, py, now = HidTouch(&px, &py);
        int tap = now && !touchPrev; touchPrev = now;
        if (!hidReady || !tap) continue;
        FBox3 f[6]; int nf = HexBuildForm(f);
        for (int i = 0; i < nf; ++i)
        {
            if (!KbHit(px, py, f[i].x, f[i].y, f[i].w, f[i].h)) continue;
            switch (f[i].code)
            {
                case 1: HexGoto(); break;
                case 2: HexEditByte(); break;
                case 3:
                    if (g_searchSelAddr) { g_hexCursor = g_searchSelAddr; HexClampCursor(); }
                    else QueueToastRaw("No Cheat Search result yet", "");
                    break;
                case 4: HexEditByte(); break;
            }
            redrawTop = 1; redrawBot = 1;
            touchPrev = HidTouch(0, 0);
            break;
        }
    }
}

// ======================= Game Guide / Plugin Guide =======================
// Reader: word-wrap a body into visual lines, then scroll through them.
#define GR_MAXLINES 2000
static u16 g_glOff[GR_MAXLINES];
static u16 g_glLen[GR_MAXLINES];
static int g_glN;
static void GuideWrap(const char *s, int cols)
{
    g_glN = 0;
    int len = 0; while (s[len]) len++;
    int i = 0;
    while (i < len && g_glN < GR_MAXLINES)
    {
        int start = i, lastSpace = -1, count = 0;
        while (i < len && count < cols && s[i] != '\n')
        {
            if (s[i] == ' ') lastSpace = i;
            i++; count++;
        }
        int end;
        if (i < len && s[i] == '\n')                  { end = i; i++; }               // hard break
        else if (i >= len)                            { end = i; }                     // end of text
        else if (count >= cols && lastSpace > start)  { end = lastSpace; i = lastSpace + 1; } // wrap at space
        else                                          { end = i; }                     // long word / hard cut
        g_glOff[g_glN] = (u16)start; g_glLen[g_glN] = (u16)(end - start); g_glN++;
    }
}

// static branded bottom panel while a guide is open
static void GuideBottom(const char *subtitle)
{
    for (int yy = 0; yy < BOT_H; ++yy)
        for (int xx = 0; xx < BOT_W; ++xx)
        {
            u8 *p = CPix(xx, yy);
            if (savedBotValid)
            { u16 v = savedBot[yy * BOT_W + xx];
              p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
            else { p[0] = p[1] = p[2] = 12; }
        }
    CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
    CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
    CText(14, 10, T("Game Guide"), GOLD, 1);
    CFill(14, 32, C6Width(T("Game Guide")) * 2, 1, GOLD);
    CText6(14, 44, subtitle, INK);   // guide content: stays English
    CText6(14, 70, T("Read on the top screen."), INK_DIM);
    CText6Btn(14, 86, T("{DP} / {L}/{R} : scroll or move"), INK_DIM);
    CText6Btn(14, 100, T("{A} open     {B} back"), INK_DIM);
    CText6(14, 114, T("SELECT : back to the game"), INK_DIM);
    BotBlitComposeBoth();
}

// Solid dark-brown window (gold border) instead of the parchment + triforce,
// so dense guide text is easy to read.
static void GuideBackdrop(void)
{
    RestoreTopBackdrop();
    CFill(WIN_X, WIN_Y, WIN_W, WIN_H, BG); // solid theme background for easy reading
    CFill(WIN_X, WIN_Y, WIN_W, 2, GOLD);
    CFill(WIN_X, WIN_Y + WIN_H - 2, WIN_W, 2, GOLD);
    CFill(WIN_X, WIN_Y, 2, WIN_H, GOLD);
    CFill(WIN_X + WIN_W - 2, WIN_Y, 2, WIN_H, GOLD);
}

// Scrollable reader. Returns 1 on B (back); sets g_quitToGame and returns 0 on SELECT.
static int GuideReader(const char *title, const char *body, int *scrollIO)
{
    int cols = (WIN_W - 30) / 7;   // chars/line at 7px advance (~41)
    GuideWrap(body, cols);
    int rows = 12, redraw = 1;
    int scroll = scrollIO ? *scrollIO : 0;
    if (scroll > g_glN) scroll = 0;
    u32 prev = HID_PAD;
    while (1)
    {
        int maxScroll = (g_glN > rows) ? g_glN - rows : 0;
        if (redraw)
        {
            GuideBackdrop();
            CText(WIN_X + 12, WIN_Y + 6, title, GOLD, 1);
            char pi[16];
            siprintf(pi, "%d%%", maxScroll ? scroll * 100 / maxScroll : 100);
            CText6(WIN_X + WIN_W - 12 - C6Width(pi), WIN_Y + 9, pi, INK_DIM);
            CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);
            for (int r = 0; r < rows; ++r)
            {
                int li = scroll + r;
                if (li >= g_glN) break;
                char buf[64];
                int len = g_glLen[li]; if (len > 63) len = 63;
                memcpy(buf, body + g_glOff[li], (size_t)len); buf[len] = 0;
                CText6(WIN_X + 14, WIN_Y + 28 + r * 13, buf, INK);
            }
            if (g_glN > rows)
            {
                int trackH = rows * 13;
                int barH = trackH * rows / g_glN; if (barH < 8) barH = 8;
                int barY = WIN_Y + 28 + (trackH - barH) * scroll / maxScroll;
                CFill(WIN_X + WIN_W - 15, WIN_Y + 28, 3, trackH, 40, 32, 20);
                CFill(WIN_X + WIN_W - 15, barY, 3, barH, GOLD);
            }
            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{DP} / {L}/{R} scroll   {B} back"), INK_DIM);
            Present(); Present();
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if ((down & BUTTON_DOWN) && scroll < maxScroll) { scroll++; redraw = 1; }
        if ((down & BUTTON_UP)   && scroll > 0)         { scroll--; redraw = 1; }
        if (down & BUTTON_R1) { scroll += rows; if (scroll > maxScroll) scroll = maxScroll; redraw = 1; }
        if (down & BUTTON_L1) { scroll -= rows; if (scroll < 0) scroll = 0; redraw = 1; }
        if (down & BUTTON_B) { if (scrollIO) *scrollIO = scroll; return 1; }
        if (down & BUTTON_SELECT) { if (scrollIO) *scrollIO = scroll; g_quitToGame = 1; return 0; }
    }
}

// Titled list with a cursor. Same look as the main menu (system font, folder
// icons, ROW_H rows). Returns chosen index, or -1 on B, -2 on SELECT (quit).
static int GuideList(const char *title, const char **labels, int count, int initSel, int *outSel)
{
    int sel = (initSel >= 0 && initSel < count) ? initSel : 0, scroll = 0, redraw = 1;
    u32 prev = HID_PAD;
    while (1)
    {
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + MAX_ROWS) scroll = sel - MAX_ROWS + 1;
        if (outSel) *outSel = sel;
        if (redraw)
        {
            ComposeBackdrop();
            int tw = CTextWidth(title);
            CText(WIN_X + 12, WIN_Y + 7, title, INK, 1);
            CFill(WIN_X + 12, WIN_Y + 24, tw + 6, 1, GOLD);
            for (int r = 0; r < MAX_ROWS; ++r)
            {
                int i = scroll + r; if (i >= count) break;
                int y = ROW_Y0 + r * ROW_H;
                if (i == sel)
                {
                    CFillBlend(ROW_X - 4, y - 1, ROW_W + 8, ROW_H, 0, 0, 0, 110);
                    CFill(ROW_X - 4, y - 1, 2, ROW_H, GOLD);
                }
                FolderIconSmall(ROW_X, y + 1);
                CText(ROW_X + 20, y - 1, labels[i], INK, 0);
            }
            if (scroll > 0)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, ROW_Y0 + 3 + a, 1 + 2 * a, 1, GOLD);
            if (scroll + MAX_ROWS < count)
                for (int a = 0; a < 4; ++a) CFill(WIN_X + WIN_W - 14 - a, ROW_Y0 + MAX_ROWS * ROW_H - 4 - a, 1 + 2 * a, 1, GOLD);
            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 16, T("{A} open   {B} back   SELECT: game"), INK_DIM);
            Present(); Present();
            redraw = 0;
        }
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        if (down & BUTTON_DOWN) { sel = (sel + 1 < count) ? sel + 1 : 0; redraw = 1; }
        if (down & BUTTON_UP)   { sel = (sel > 0) ? sel - 1 : count - 1; redraw = 1; }
        if (down & BUTTON_A)      return sel;
        if (down & BUTTON_B)      return -1;
        if (down & BUTTON_SELECT) return -2;
    }
}

static const char *GUIDE_CREDITS =
    "This walkthrough is included with credit to the people who made it.\n"
    "\n"
    "Walkthrough text:\n"
    "  z64central.com\n"
    "  (Zelda Ocarina 3D Help)\n"
    "\n"
    "Packaged for the CTRPF plugin by:\n"
    "  LowEndC  (GBAtemp member)\n"
    "  gbatemp.net/threads/422574\n"
    "  post #500, Feb 2019\n"
    "\n"
    "Original OoT3D cheats plugin:\n"
    "  Nanquitas (GBAtemp / GitHub)\n"
    "\n"
    "This build (OcarinaCTRComposer) reproduces the guide only to credit the\n"
    "original authors. The Legend of Zelda and all game content are\n"
    "(c) Nintendo. Fan project, non-commercial.";

// Navigation state persists so that SELECT-to-game then SELECT-back returns you
// to the exact page and scroll position you were reading.
// mode: 0 = category list, 1 = page list, 2 = reader, 3 = credits reader.
static int g_ggMode = 0, g_ggCatCur = 0, g_ggCat = 0, g_ggPage = 0, g_ggScroll = 0, g_ggCredScroll = 0;
static void ToolGameGuide(void)
{
    GuideBottom("Ocarina of Time 3D");
    while (1)
    {
        int ncats; const GuideCat *cats = GG_Cats(&ncats);
        if (g_ggCat >= ncats) g_ggCat = 0;              // language switch may shrink the set
        if (g_ggMode == 2) // reading a category page
        {
            if (g_ggPage >= cats[g_ggCat].nPages) g_ggPage = 0;
            const GuidePage *pg = &cats[g_ggCat].pages[g_ggPage];
            int r = GuideReader(pg->title, pg->body, &g_ggScroll);
            if (r == 0) return;          // SELECT: stay at mode 2 -> resume here next time
            g_ggMode = 1;                // B -> page list
        }
        else if (g_ggMode == 3) // reading Credits
        {
            int r = GuideReader("Credits", GUIDE_CREDITS, &g_ggCredScroll);
            if (r == 0) return;
            g_ggMode = 0;
        }
        else if (g_ggMode == 0) // category list
        {
            const char *labels[SDG_MAXCATS + 1];
            for (int i = 0; i < ncats; ++i) labels[i] = cats[i].title;
            labels[ncats] = "Credits";
            int r = GuideList(T("Game Guide"), labels, ncats + 1, g_ggCatCur, &g_ggCatCur);
            if (r == -2) { g_quitToGame = 1; return; } // stay at mode 0 -> resume the list
            if (r == -1) return;
            if (r == ncats) g_ggMode = 3;              // Credits
            else { g_ggCat = r; g_ggMode = 1; }
        }
        else // page list
        {
            const GuideCat *c = &cats[g_ggCat];
            const char *labels[20];
            int n = c->nPages; if (n > 20) n = 20;
            for (int i = 0; i < n; ++i) labels[i] = c->pages[i].title;
            int r = GuideList(c->title, labels, n, g_ggPage, &g_ggPage);
            if (r == -2) { g_quitToGame = 1; return; }
            if (r == -1) { g_ggMode = 0; continue; }
            g_ggPage = r; g_ggScroll = 0; g_ggMode = 2; // open the page from the top
        }
    }
}

// ---- Plugin Guide (original content, explains this plugin) ----
static const GuidePage PLUGIN_PAGES[] = {
    { "Overview",
      "OcarinaCTRComposer is an overlay for The Legend of Zelda: Ocarina of Time 3D.\n"
      "\n"
      "Press SELECT during the game to open the menu. The game pauses while the\n"
      "menu is open. Press SELECT again (from anywhere) to jump straight back to\n"
      "the game.\n"
      "\n"
      "Navigate with the D-Pad. A opens a folder or toggles a cheat. B goes back\n"
      "one level. X shows info about the selected item. Y stars a favorite." },
    { "Quick Menu & Favorites",
      "Star your most-used cheats with Y in the menu. Then hold L+SELECT (or\n"
      "R+SELECT) to open the Quick Menu: a compact list of just your favorites,\n"
      "without opening the full menu.\n"
      "\n"
      "The hotkey can be changed in Settings. Favorites, the toast toggle and the\n"
      "hotkey are saved to the SD card and survive a reboot." },
    { "Cheat Search",
      "Find the memory address of any value, then change it.\n"
      "\n"
      "Known Value: type a number you can see (e.g. your rupees), Search, then\n"
      "narrow the results as the value changes (Greater / Less / Changed...).\n"
      "\n"
      "Unknown Search: don't know the number? Take a snapshot, change the value\n"
      "in the game, then scan Increased / Decreased / Changed to close in on it.\n"
      "\n"
      "The real loop: Search, press SELECT to return to the game, change the\n"
      "value, SELECT to reopen (results are kept), scan again. Repeat until a few\n"
      "results remain. Press A on a result to poke a new value. L undoes a scan." },
    { "RAM Dumper",
      "Save a block of the game's memory to a .bin file on the SD card.\n"
      "\n"
      "Set a Start address (or press Y / From Search to pull the address you\n"
      "found in Cheat Search) and a Size, then Dump. Files are written to\n"
      "sdmc:/luma/plugins/<titleid>/dumps/.\n"
      "\n"
      "The tool only writes memory that is actually readable, so it never\n"
      "crashes on an unmapped address. Great for studying the bytes around a\n"
      "value you found." },
    { "Hex Editor",
      "Browse memory as a live hex grid and edit any byte on the spot.\n"
      "\n"
      "D-Pad moves the cursor (left/right one byte, up/down one row). L/R page\n"
      "up and down. X jumps to an address; Y jumps to your Cheat Search result.\n"
      "Press A to edit the byte under the cursor.\n"
      "\n"
      "Read-only regions are protected: editing there is refused instead of\n"
      "crashing. Unreadable bytes show as --." },
    { "Tips",
      "- SELECT is always 'back to the game', from any screen.\n"
      "- Reopening the menu returns you to where you were, even inside a tool.\n"
      "- Code-patch cheats (like Invincible) are never auto-enabled on boot.\n"
      "- Toast notifications can be turned off in Settings." },
};
#define PLUGIN_NPAGES ((int)(sizeof(PLUGIN_PAGES) / sizeof(PLUGIN_PAGES[0])))

// Return the active guide model: SD translation if loaded, else embedded English.
static const GuideCat *GG_Cats(int *n)
{
    if (g_ggNCats) { *n = g_ggNCats; return g_ggCatsBuf; }
    *n = GUIDE_NCATS; return GUIDE_CATS;
}
static const GuidePage *PG_Pages(int *n)
{
    if (g_pgNPages) { *n = g_pgNPages; return g_pgPagesBuf; }
    *n = PLUGIN_NPAGES; return PLUGIN_PAGES;
}

static int g_pgMode = 0, g_pgCur = 0, g_pgPage = 0, g_pgScroll = 0; // resume state
static void ToolPluginGuide(void)
{
    GuideBottom("How to use OcarinaCTRComposer");
    while (1)
    {
        int npg; const GuidePage *pages = PG_Pages(&npg);
        if (g_pgPage >= npg) g_pgPage = 0;
        if (g_pgMode == 1) // reading a page
        {
            int r = GuideReader(pages[g_pgPage].title, pages[g_pgPage].body, &g_pgScroll);
            if (r == 0) return;   // SELECT: resume here
            g_pgMode = 0;
        }
        else
        {
            const char *labels[32];
            int n = npg; if (n > 32) n = 32;
            for (int i = 0; i < n; ++i) labels[i] = pages[i].title;
            int r = GuideList(T("Plugin Guide"), labels, n, g_pgCur, &g_pgCur);
            if (r == -2) { g_quitToGame = 1; return; }
            if (r == -1) return;
            g_pgPage = r; g_pgScroll = 0; g_pgMode = 1;
        }
    }
}

// ===================== Checklist 100% (progression-ordered, mixed item types) =====================
// v1 TEST DATASET: 2 of the ~14 planned progression areas (Kokiri Forest & Deku Tree; Hyrule
// Castle & Kakariko), enough to validate the whole mechanism end-to-end on hardware before
// authoring the rest. Adding more areas later is pure data (append a ChkCat) - the UI code below
// doesn't know or care how many there are.
//
// Detection "kind" (Auto-fill). Monotonic: only ever sets state 0->1, NEVER clears a mark - a
// consumed item can't silently un-tick real progress, and a manual un-check (state 2->3) is the
// only way an item goes back to unchecked; Auto-fill never touches state 1/2/3.
//   CK_MANUAL  - never auto-fillable (no reliable RAM signal known yet)
//   CK_EQUIP   - (R8(addr) & mask) != 0. A single confirmed bit. Two confirmed sources:
//                (a) equipment bits (swords/shields/tunics) - same byte/nibble the equip-toggle
//                cheats read/write; (b) the quest-status bitfield at 0x0587A14 (medallions bits
//                0-5, the 10 songs in bits 6-15/0x0587A15 + bits 16-17, spiritual stones bits
//                18-20/0x0587A16). The quest bit->item map was verified EMPIRICALLY by diffing 48
//                progressive OoT3D save files (HTW set): each newly-set bit lines up exactly with
//                the song/medallion/stone that save gained. Not a guess.
//   CK_QUESTALL- R8(addr) == mask. An exact "all N obtained" aggregate. Unused now that the
//                per-bit quest map is confirmed, but kept for the pattern.
//   CK_INVSLOT - the item lives in the game's fixed 24-slot inventory array at 0x5879E4 (proven
//                by the "All Items" cheat, which memcpys a 30-byte layout there). A slot holds the
//                item's ID, or 0xFF when empty. mask==0 -> present if R8(addr) != 0xFF (any item
//                in that slot); mask!=0 -> present only if R8(addr) == mask (an exact ID, for
//                slots shared by a child/adult pair, e.g. Hookshot 0x0A vs Longshot 0x0B).
//                Can only false-NEGATIVE (a consumable slot clearing), never false-positive, so
//                it's safe: a miss just leaves the item manual, exactly like today.
//   CK_UPGRADE - a multi-bit capacity field inside the "upgrade word" u32 at 0x0587A10. addr is
//                that u32; mask packs (shift<<3)|minLevel. got = ((R32(addr)>>shift)&0x7) >= min.
//                Field layout confirmed by save-diffing the HTW saves (each single-upgrade save
//                bumps exactly one field): quiver@0, bombBag@3, strength@6, scale@9, wallet@12,
//                bulletBag@14, stick@17, nut@20. minLevel lets "Silver Gauntlets" mean strength>=2.
enum { CK_MANUAL = 0, CK_EQUIP = 1, CK_QUESTALL = 2, CK_INVSLOT = 3, CK_UPGRADE = 4 };
enum { CKI_SPRITE, CKI_HEART, CKI_SKULL, CKI_NOTE, CKI_KEYITEM, CKI_NONE };

typedef struct {
    const char *key;    // stable save-key, never shown (so item text can be edited freely later)
    const char *task, *hint, *loc; // loc = "" -> no location to reveal
    u8 iconKind; u16 iconArg; u8 kind; // iconArg is u16: sprite keys 0x100+ (UI extras) exceed u8
    u32 addr; u8 mask;
} ChkItem;
typedef struct { const char *name; const ChkItem *items; int count; } ChkCat;

static const ChkItem CK_C0[] = { // Kokiri Forest & Deku Tree
    { "c0_swkokiri", "Kokiri Sword",    "Thru the tunnel behind the training area.", "Kokiri Forest - training ground, past the fences.", CKI_SPRITE, 0x3B, CK_EQUIP, 0x0587A0E, 0x01 },
    { "c0_shdeku",   "Deku Shield",     "Buy it at the shop with the red roof (40 rupees).", "Kokiri Forest shop.", CKI_SPRITE, 0x3E, CK_EQUIP, 0x0587A0E, 0x10 },
    { "c0_stick",    "Deku Stick",      "Picked up in Kokiri Forest.", "Kokiri Forest.", CKI_SPRITE, 0x00, CK_INVSLOT, 0x5879E4, 0 },
    { "c0_nut",      "Deku Nut",        "Found inside the Great Deku Tree.", "Great Deku Tree - early room.", CKI_SPRITE, 0x01, CK_INVSLOT, 0x5879E5, 0 },
    { "c0_map",      "Dungeon Map",     "Great Deku Tree.", "Great Deku Tree.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A18, 0x04 },
    { "c0_sling",    "Fairy Slingshot", "Great Deku Tree, main room.", "Great Deku Tree.", CKI_SPRITE, 0x06, CK_INVSLOT, 0x5879EA, 0 },
    { "c0_compass",  "Compass",         "Great Deku Tree.", "Great Deku Tree.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A18, 0x02 },
    { "c0_gs1",      "Gold Skulltula 1", "In the compass room.", "Great Deku Tree - compass room.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c0_gs2",      "Gold Skulltula 2", "Lower level, on the vines.", "Great Deku Tree - lower level.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c0_gs3",      "Gold Skulltula 3", "Lower level, on the grates.", "Great Deku Tree - lower level.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c0_hc1",      "Heart Container", "Defeat Queen Gohma.", "Great Deku Tree - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A16, 0x04 },
    { "c0_ocarina",  "Fairy Ocarina",   "Talk to Saria on your way out of the forest.", "Kokiri Forest exit.", CKI_SPRITE, 0x07, CK_INVSLOT, 0x5879EB, 0 },
};
static const ChkItem CK_C1[] = { // Hyrule Castle & Kakariko Village
    { "c1_gs1",      "Gold Skulltula 1", "In a crate in the guardhouse.", "Hyrule Castle Market - guardhouse.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c1_gs2",      "Gold Skulltula 2", "First tree in the area.", "Hyrule Castle grounds.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c1_letter",   "Zelda's Letter",  "Get caught sneaking into the castle, then talk to Malon.", "Hyrule Castle grounds.", CKI_SPRITE, 0x23, CK_MANUAL, 0, 0 },
    { "c1_song1",    "Song: Zelda's Lullaby", "Impa teaches it before Kakariko.", "Hyrule Castle grounds.", CKI_NOTE, 0, CK_EQUIP, 0x0587A15, 0x10 },
    { "c1_song2",    "Song: Sun's Song",      "Learn it inside the Royal Tomb.", "Kakariko Graveyard.", CKI_NOTE, 0, CK_EQUIP, 0x0587A15, 0x80 },
    { "c1_hp1",      "Heart Piece 1",   "Inside the 4th grave from the right, near the Royal Tomb.", "Kakariko Graveyard.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c1_shhyl",    "Hylian Shield",   "Inside a grave in the front row (three flowers in front).", "Kakariko Graveyard.", CKI_SPRITE, 0x3F, CK_EQUIP, 0x0587A0E, 0x20 },
    { "c1_bottle1",  "Bottle #1",       "Win Talon's cow-catching minigame (10 rupees).", "Lon Lon Ranch.", CKI_SPRITE, 0x14, CK_INVSLOT, 0x5879F6, 0 },
    { "c1_gs3",      "Gold Skulltula 3", "Tree behind the farm buildings.", "Lon Lon Ranch.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c1_song3",    "Song: Epona's Song", "Play your Ocarina for Malon.", "Lon Lon Ranch.", CKI_NOTE, 0, CK_EQUIP, 0x0587A15, 0x20 },
    { "c1_gs4",      "Gold Skulltula 4", "Behind the feeding trough, at night.", "Lon Lon Ranch (night).", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c1_hp2",      "Heart Piece 2",   "Inside the silo, through the tunnel behind the crates.", "Lon Lon Ranch.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c1_gs5",      "Gold Skulltula 5", "Back of the Know-It-All Brothers' house, at night.", "Kokiri Forest (night).", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c1_seedbag",  "Deku Seed Bullet Bag", "Shoot the target 3 times (score 100 each).", "Lost Woods entrance.", CKI_SPRITE, 0x101, CK_UPGRADE, 0x0587A10, (14 << 3) | 1 },
    { "c1_hp3",      "Heart Piece 3",   "Play the follow-along minigame with your Ocarina.", "Lost Woods.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c1_song4",    "Song: Saria's Song", "Talk to Saria after navigating the Lost Woods.", "Sacred Forest Meadow.", CKI_NOTE, 0, CK_EQUIP, 0x0587A15, 0x40 },
};
static const ChkItem CK_C2[] = { // Zora's Domain & Jabu-Jabu's Belly
    { "c2_letter",   "Ruto's Letter",       "Dive to the bottom of Lake Hylia to find a bottle with a letter inside.", "Lake Hylia - lakebed.", CKI_SPRITE, 0x1B, CK_MANUAL, 0, 0 },
    { "c2_kingzora", "Pass King Zora",      "Show the letter to King Zora in his throne room; he slides aside to open the way to Zora's Fountain.", "Zora's Domain - King Zora's chamber.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c2_jabu",     "Enter Jabu-Jabu's Belly", "Catch a fish in a bottle and offer it to Lord Jabu-Jabu; he'll swallow you whole.", "Zora's Fountain.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c2_map",      "Dungeon Map",         "Jabu-Jabu's Belly.", "Jabu-Jabu's Belly.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A1A, 0x04 },
    { "c2_compass",  "Compass",             "Jabu-Jabu's Belly.", "Jabu-Jabu's Belly.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A1A, 0x02 },
    { "c2_boomerang","Boomerang",           "Found in a chest inside the dungeon; needed to stun enemies and pull switches.", "Jabu-Jabu's Belly.", CKI_SPRITE, 0x0E, CK_INVSLOT, 0x5879F0, 0 },
    { "c2_ruto",     "Rescue Princess Ruto","Find her trapped behind a switch-locked cage partway through the dungeon.", "Jabu-Jabu's Belly - Ruto's cage room.", CKI_KEYITEM, 0, CK_EQUIP, 0x0587A16, 0x10 },
    { "c2_sapphire", "Zora's Sapphire",     "Ruto hands it over once you've freed her and are heading toward the boss door.", "Jabu-Jabu's Belly.", CKI_SPRITE, 0x10C, CK_EQUIP, 0x0587A16, 0x10 },
    { "c2_gs1",      "Gold Skulltula 1",    "Stuck to the ladder at the waterfall leading up into Zora's Domain, at night.", "Zora's River - waterfall ladder.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c2_gs2",      "Gold Skulltula 2",    "Jump onto the fence by the wooden bridge and look up at the wall, at night.", "Zora's River - bridge fence.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c2_gs3",      "Gold Skulltula 3",    "Near the top of the frozen waterfall past King Zora's chamber, at night.", "Zora's Domain.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c2_heart",    "Heart Piece",         "Step on the log jutting into the river and play the Song of Storms for the frogs.", "Zora's River - frog log.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c2_hc",       "Heart Container",     "Defeat Barinade.", "Jabu-Jabu's Belly - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A16, 0x10 },
};
static const ChkItem CK_C3[] = { // Death Mountain (Goron City & Dodongo's Cavern)
    { "c3_gorondoor","Meet Darunia",        "Play Zelda's Lullaby in front of the stone door on Goron City's bottom floor.", "Goron City.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c3_dodongo",  "Enter Dodongo's Cavern", "Roll a bomb flower into the boulder blocking the cavern entrance to blast it open.", "Death Mountain Trail - cavern entrance.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c3_map",      "Dungeon Map",         "Dodongo's Cavern.", "Dodongo's Cavern.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A19, 0x04 },
    { "c3_compass",  "Compass",             "Dodongo's Cavern.", "Dodongo's Cavern.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A19, 0x02 },
    { "c3_bombbag",  "Bomb Bag",            "Found in a chest guarded by a Lizalfos in the cavern's central room.", "Dodongo's Cavern.", CKI_SPRITE, 0x02, CK_UPGRADE, 0x0587A10, (3 << 3) | 1 },
    { "c3_gs1",      "Gold Skulltula 1",    "Bomb the cracked wall along the trail between Kakariko and the cavern entrance.", "Death Mountain Trail.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c3_gs2",      "Gold Skulltula 2",    "Near the summit, hiding behind a boulder that needs the Megaton Hammer, at night.", "Death Mountain Trail - summit path.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c3_gs3",      "Gold Skulltula 3",    "Bomb into the side room in Goron City and roll into the crate at the back.", "Goron City.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c3_heart1",   "Heart Piece 1",       "Plant a Magic Bean at the cavern entrance, then return as an adult and ride the stalk up.", "Death Mountain Trail - cavern entrance.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c3_heart2",   "Heart Piece 2",       "Backflip off the low fence near the cavern entrance.", "Death Mountain Trail.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c3_hc",       "Heart Container",     "Defeat King Dodongo.", "Dodongo's Cavern - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A16, 0x08 },
    { "c3_ruby",     "Goron's Ruby",        "Darunia gives it to you after you defeat King Dodongo.", "Dodongo's Cavern.", CKI_SPRITE, 0x118, CK_EQUIP, 0x0587A16, 0x08 },
};
static const ChkItem CK_C4[] = { // Lake Hylia & Gerudo Valley
    { "c4_gs1",      "Gold Skulltula 1",    "Drop a bug into the soft soil by the river, below the second bridge.", "Gerudo Valley.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c4_gs2",      "Gold Skulltula 2",    "Near the entrance, by the wooden bridge and mini waterfall, at night.", "Gerudo Valley.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c4_gs3",      "Gold Skulltula 3",    "On the small island far out in the lake, at night.", "Lake Hylia.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c4_gs4",      "Gold Skulltula 4",    "In a patch of dirt near the Lakeside Laboratory.", "Lake Hylia - Lakeside Laboratory.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c4_gs5",      "Gold Skulltula 5",    "Behind the Lakeside Laboratory at night; use the Boomerang to snag it.", "Lake Hylia - Lakeside Laboratory.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c4_heart1",   "Heart Piece 1",       "Bomb the center of the fenced-in grassy area near the lake's entrance to reveal a hole.", "Lake Hylia - entrance.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c4_heart2",   "Heart Piece 2",       "Plant a Magic Bean next to the Lakeside Laboratory, then return as an adult and climb the stalk.", "Lake Hylia - Lakeside Laboratory.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c4_heart3",   "Heart Piece 3",       "Grab a Cucco and float behind the big waterfall, then climb the ladder.", "Gerudo Valley - waterfall.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c4_heart4",   "Heart Piece 4",       "Float across on a Cucco to the small platform and smash the crate.", "Gerudo Valley.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c4_fishing",  "Fishing Pond",        "Rent a rod at the Fishing Pond and try landing a big catch.", "Lake Hylia - Fishing Pond.", CKI_SPRITE, 0x119, CK_MANUAL, 0, 0 },
};
static const ChkItem CK_C5[] = { // Becoming an Adult
    { "c5_present",  "Open the Door of Time","Place all three Spiritual Stones on the altar and play the Song of Time.", "Temple of Time.", CKI_KEYITEM, 0, CK_EQUIP, 0x0587A0E, 0x02 },
    { "c5_mastersword","Master Sword",      "Pull it from its pedestal beyond the Door of Time.", "Temple of Time - Sacred realm pedestal.", CKI_SPRITE, 0x3C, CK_EQUIP, 0x0587A0E, 0x02 },
    { "c5_timeskip", "Awaken as an Adult",  "Seven years pass in an instant the moment you draw the Master Sword.", "Temple of Time.", CKI_KEYITEM, 0, CK_EQUIP, 0x0587A0E, 0x02 },
    { "c5_sheik",     "Meet Sheik",         "A mysterious figure appears in the Temple of Time as you awaken.", "Temple of Time.", CKI_KEYITEM, 0, CK_EQUIP, 0x0587A0E, 0x02 },
    { "c5_prelude",  "Song: Prelude of Light", "Taught by Sheik right after you wake up as an adult.", "Temple of Time.", CKI_NOTE, 6, CK_EQUIP, 0x0587A15, 0x08 },
    { "c5_rauru",    "Rauru, Sage of Light","He awakens and grants you his medallion the moment you meet him.", "Temple of Time - Chamber of Sages.", CKI_SPRITE, 0x117, CK_EQUIP, 0x0587A14, 0x20 },
    { "c5_lightmed", "Light Medallion",     "Given automatically the moment Rauru awakens as the Sage of Light.", "Temple of Time - Chamber of Sages.", CKI_SPRITE, 0x117, CK_EQUIP, 0x0587A14, 0x20 },
};
static const ChkItem CK_C6[] = { // Forest Temple
    { "c6_minuet",   "Song: Minuet of Forest", "Taught by Sheik in the Sacred Forest Meadow, right before the temple entrance.", "Sacred Forest Meadow.", CKI_NOTE, 1, CK_EQUIP, 0x0587A14, 0x40 },
    { "c6_map",      "Dungeon Map",         "Forest Temple.", "Forest Temple.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A1B, 0x04 },
    { "c6_compass",  "Compass",             "Forest Temple.", "Forest Temple.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A1B, 0x02 },
    { "c6_bosskey",  "Boss Key",            "Forest Temple.", "Forest Temple.", CKI_SPRITE, 0x10A, CK_EQUIP, 0x0587A1B, 0x01 },
    { "c6_bow",      "Fairy Bow",           "Found in a chest after solving the eye-switch puzzle in the well room.", "Forest Temple.", CKI_SPRITE, 0x03, CK_INVSLOT, 0x5879E7, 0 },
    { "c6_gs1",      "Gold Skulltula 1",    "In the block-pushing courtyard near the entrance.", "Forest Temple - west courtyard.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c6_heart",    "Heart Piece",         "In the west courtyard, reached by pushing the two-story block up to the ledge.", "Forest Temple - west courtyard.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c6_hc",       "Heart Container",     "Defeat Phantom Ganon.", "Forest Temple - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A14, 0x01 },
    { "c6_medal",    "Forest Medallion",    "Given automatically once Phantom Ganon falls.", "Forest Temple - boss room.", CKI_SPRITE, 0x10B, CK_EQUIP, 0x0587A14, 0x01 },
};
static const ChkItem CK_C7[] = { // Fire Temple
    { "c7_bolero",   "Song: Bolero of Fire","Taught by Sheik on Death Mountain Crater, near the temple entrance.", "Death Mountain Crater.", CKI_NOTE, 2, CK_EQUIP, 0x0587A14, 0x80 },
    { "c7_map",      "Dungeon Map",         "Fire Temple.", "Fire Temple.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A1C, 0x04 },
    { "c7_compass",  "Compass",             "Fire Temple.", "Fire Temple.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A1C, 0x02 },
    { "c7_bosskey",  "Boss Key",            "Fire Temple.", "Fire Temple.", CKI_SPRITE, 0x10A, CK_EQUIP, 0x0587A1C, 0x01 },
    { "c7_hammer",   "Megaton Hammer",      "Found in a chest guarded by two Flare Dancers.", "Fire Temple.", CKI_SPRITE, 0x11, CK_INVSLOT, 0x5879F3, 0 },
    { "c7_gs1",      "Gold Skulltula 1",    "In a side room, reachable by crossing narrow ledges above the lava.", "Fire Temple.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c7_heart",    "Heart Piece",         "In a side chamber reached by smashing a switch across a lava pit with the Megaton Hammer.", "Fire Temple.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c7_hc",       "Heart Container",     "Defeat Volvagia.", "Fire Temple - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A14, 0x02 },
    { "c7_medal",    "Fire Medallion",      "Given automatically once Volvagia falls.", "Fire Temple - boss room.", CKI_SPRITE, 0x113, CK_EQUIP, 0x0587A14, 0x02 },
};
static const ChkItem CK_C8[] = { // Water Temple
    { "c8_serenade", "Song: Serenade of Water", "Taught by Sheik at Lake Hylia before you dive down to the temple entrance.", "Lake Hylia.", CKI_NOTE, 3, CK_EQUIP, 0x0587A15, 0x01 },
    { "c8_map",      "Dungeon Map",         "Water Temple.", "Water Temple.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A1D, 0x04 },
    { "c8_compass",  "Compass",             "Water Temple.", "Water Temple.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A1D, 0x02 },
    { "c8_bosskey",  "Boss Key",            "Water Temple.", "Water Temple.", CKI_SPRITE, 0x10A, CK_EQUIP, 0x0587A1D, 0x01 },
    { "c8_longshot", "Longshot",            "Found in a chest guarded by Dark Link.", "Water Temple.", CKI_SPRITE, 0x0B, CK_INVSLOT, 0x5879ED, 0x0B },
    { "c8_gs1",      "Gold Skulltula 1",    "In the room behind the central pillar, once the water level is raised.", "Water Temple.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c8_gs2",      "Gold Skulltula 2",    "In one of the side corridors reachable with Iron Boots.", "Water Temple.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c8_heart",    "Heart Piece",         "Behind a locked door, opened by hitting a switch with the Longshot at the highest water level.", "Water Temple.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c8_hc",       "Heart Container",     "Defeat Morpha.", "Water Temple - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A14, 0x04 },
    { "c8_medal",    "Water Medallion",     "Given automatically once Morpha falls.", "Water Temple - boss room.", CKI_SPRITE, 0x114, CK_EQUIP, 0x0587A14, 0x04 },
};
static const ChkItem CK_C9[] = { // Shadow Temple
    { "c9_nocturne", "Song: Nocturne of Shadow", "Taught by Sheik at the Kakariko graveyard, in front of the temple entrance.", "Kakariko Village - graveyard.", CKI_NOTE, 5, CK_EQUIP, 0x0587A15, 0x04 },
    { "c9_map",      "Dungeon Map",         "Shadow Temple.", "Shadow Temple.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A1F, 0x04 },
    { "c9_compass",  "Compass",             "Shadow Temple.", "Shadow Temple.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A1F, 0x02 },
    { "c9_bosskey",  "Boss Key",            "Shadow Temple.", "Shadow Temple.", CKI_SPRITE, 0x10A, CK_EQUIP, 0x0587A1F, 0x01 },
    { "c9_hoverboots","Hover Boots",        "Found in a chest across the invisible bridge over the pit of skulls.", "Shadow Temple.", CKI_SPRITE, 0x110, CK_EQUIP, 0x0587A0F, 0x40 },
    { "c9_gs1",      "Gold Skulltula 1",    "In the spike-trap corridor; use the Lens of Truth to spot it.", "Shadow Temple.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c9_heart",    "Heart Piece",         "In a chest along the spike-trap corridor, reachable with the Hover Boots.", "Shadow Temple.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c9_hc",       "Heart Container",     "Defeat Bongo Bongo.", "Shadow Temple - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A14, 0x10 },
    { "c9_medal",    "Shadow Medallion",    "Given automatically once Bongo Bongo falls.", "Shadow Temple - boss room.", CKI_SPRITE, 0x116, CK_EQUIP, 0x0587A14, 0x10 },
};
static const ChkItem CK_C10[] = { // Spirit Temple
    { "c10_requiem", "Song: Requiem of Spirit", "Taught by Sheik as you first arrive at the Desert Colossus.", "Desert Colossus.", CKI_NOTE, 4, CK_EQUIP, 0x0587A15, 0x02 },
    { "c10_map",     "Dungeon Map",         "Spirit Temple.", "Spirit Temple.", CKI_SPRITE, 0x108, CK_EQUIP, 0x0587A1E, 0x04 },
    { "c10_compass", "Compass",             "Spirit Temple.", "Spirit Temple.", CKI_SPRITE, 0x109, CK_EQUIP, 0x0587A1E, 0x02 },
    { "c10_bosskey", "Boss Key",            "Spirit Temple.", "Spirit Temple.", CKI_SPRITE, 0x10A, CK_EQUIP, 0x0587A1E, 0x01 },
    { "c10_gauntlets","Silver Gauntlets",   "Found in the hand statue's chest after solving the child/adult puzzles.", "Spirit Temple.", CKI_SPRITE, 0x10E, CK_UPGRADE, 0x0587A10, (6 << 3) | 2 },
    { "c10_mirror",  "Mirror Shield",       "Found in a chest guarded by Beamos and Anubis statues.", "Spirit Temple.", CKI_SPRITE, 0x40, CK_EQUIP, 0x0587A0E, 0x40 },
    { "c10_gs1",     "Gold Skulltula 1",    "In the sun-block puzzle room, reflect sunlight to open a locked door.", "Spirit Temple.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c10_heart",   "Heart Piece",         "In the sun-block puzzle room, behind the door opened by the reflected beam.", "Spirit Temple.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c10_hc",      "Heart Container",     "Defeat Twinrova.", "Spirit Temple - boss room.", CKI_SPRITE, 0x102, CK_EQUIP, 0x0587A14, 0x08 },
    { "c10_medal",   "Spirit Medallion",    "Given automatically once Twinrova falls.", "Spirit Temple - boss room.", CKI_SPRITE, 0x115, CK_EQUIP, 0x0587A14, 0x08 },
};
static const ChkItem CK_C11[] = { // Gerudo's Fortress & Haunted Wasteland
    { "c11_carpenters","Rescue the Carpenters", "Sneak through the fortress and free all four captured carpenters from their cells.", "Gerudo's Fortress.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c11_card",    "Gerudo's Membership Card", "Aveil gives it to you once all four carpenters are freed.", "Gerudo's Fortress.", CKI_SPRITE, 0x112, CK_MANUAL, 0, 0 },
    { "c11_archery", "Horseback Archery: 1000 pts", "Score 1000 or more points at the archery range to win a Heart Piece.", "Gerudo's Fortress - archery range.", CKI_SPRITE, 0x103, CK_MANUAL, 0, 0 },
    { "c11_gs1",     "Gold Skulltula 1",    "On the far target at the Horseback Archery range, at night.", "Gerudo's Fortress - archery range.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c11_wasteland","Cross the Haunted Wasteland", "Follow the guiding Poe (or the landmarks) through the quicksand to reach the far side.", "Haunted Wasteland.", CKI_KEYITEM, 0, CK_EQUIP, 0x0587A14, 0x08 },
    { "c11_colossus","Reach the Desert Colossus", "Push through the sandstorm at the end of the wasteland.", "Desert Colossus.", CKI_KEYITEM, 0, CK_EQUIP, 0x0587A14, 0x08 },
};
static const ChkItem CK_C12[] = { // Fishing & Fairies
    { "c12_trade",   "Trade Sequence: Complete", "Carry the chain of trade items from the Goron all the way to Biggoron.", "Death Mountain Trail - Biggoron.", CKI_SPRITE, 0x7C, CK_INVSLOT, 0x05879FA, 0x37 },
    { "c12_biggoron","Biggoron's Sword",    "Biggoron forges it for you once the trade sequence is complete.", "Death Mountain Trail - Biggoron.", CKI_SPRITE, 0x7C, CK_EQUIP, 0x0587A0E, 0x04 },
    { "c12_fishchild","Fishing Pond Record (Child)", "Land the biggest fish you can as child Link.", "Lake Hylia - Fishing Pond.", CKI_SPRITE, 0x19, CK_MANUAL, 0, 0 },
    { "c12_fishadult","Fishing Pond Record (Adult)", "Land the biggest fish you can as adult Link.", "Lake Hylia - Fishing Pond.", CKI_SPRITE, 0x19, CK_MANUAL, 0, 0 },
    { "c12_cow",     "Cow Reward",          "Play Epona's Song in a barn, or claim the free cow from the House of Skulltula.", "Lon Lon Ranch / Kakariko Village.", CKI_SPRITE, 0x1A, CK_MANUAL, 0, 0 },
    { "c12_magic",   "Magic Meter",         "Play Zelda's Lullaby for the Great Fairy inside Death Mountain to receive your first Magic Meter.", "Death Mountain Trail - Great Fairy Fountain.", CKI_SPRITE, 0x104, CK_EQUIP, 0x05879A6, 0xFF },
    { "c12_defense", "Double Defense",      "Lift the boulder with the Golden Gauntlets to reach the Great Fairy outside Ganon's Castle.", "Ganon's Castle - exterior.", CKI_SPRITE, 0x102, CK_EQUIP, 0x05879AA, 0x01 },
};
static const ChkItem CK_C13[] = { // Ganon's Castle
    { "c13_trials",  "Clear the Six Trials","Fight through the Forest, Fire, Water, Shadow, Spirit and Light Trials to reach the tower.", "Ganon's Castle.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c13_gs1",     "Gold Skulltula",      "Hanging from the ceiling in the middle of the Light Trial room.", "Ganon's Castle - Light Trial.", CKI_SPRITE, 0x106, CK_MANUAL, 0, 0 },
    { "c13_tower",   "Climb Ganon's Tower", "Race up the collapsing tower after the trials are cleared.", "Ganon's Castle - tower.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c13_ganondorf","Defeat Ganondorf",   "Use Light Arrows and the Master Sword to bring him down.", "Ganon's Castle - tower top.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
    { "c13_ganon",   "Defeat Ganon",        "The final battle as the castle collapses around you.", "Outside the crumbling tower.", CKI_KEYITEM, 0, CK_MANUAL, 0, 0 },
};
static const ChkCat CHK_CATS[] = {
    { "Kokiri & Deku Tree",                     CK_C0,  (int)(sizeof(CK_C0)  / sizeof(CK_C0[0])) },
    { "Castle & Kakariko",                      CK_C1,  (int)(sizeof(CK_C1)  / sizeof(CK_C1[0])) },
    { "Zora's Domain & Jabu's Belly",           CK_C2,  (int)(sizeof(CK_C2)  / sizeof(CK_C2[0])) },
    { "Death Mountain",                         CK_C3,  (int)(sizeof(CK_C3)  / sizeof(CK_C3[0])) },
    { "Lake Hylia & Gerudo",                    CK_C4,  (int)(sizeof(CK_C4)  / sizeof(CK_C4[0])) },
    { "Becoming an Adult",                      CK_C5,  (int)(sizeof(CK_C5)  / sizeof(CK_C5[0])) },
    { "Forest Temple",                          CK_C6,  (int)(sizeof(CK_C6)  / sizeof(CK_C6[0])) },
    { "Fire Temple",                            CK_C7,  (int)(sizeof(CK_C7)  / sizeof(CK_C7[0])) },
    { "Water Temple",                           CK_C8,  (int)(sizeof(CK_C8)  / sizeof(CK_C8[0])) },
    { "Shadow Temple",                          CK_C9,  (int)(sizeof(CK_C9)  / sizeof(CK_C9[0])) },
    { "Spirit Temple",                          CK_C10, (int)(sizeof(CK_C10) / sizeof(CK_C10[0])) },
    { "Gerudo's Fortress",                      CK_C11, (int)(sizeof(CK_C11) / sizeof(CK_C11[0])) },
    { "Fishing & Fairies",                      CK_C12, (int)(sizeof(CK_C12) / sizeof(CK_C12[0])) },
    { "Ganon's Castle",                         CK_C13, (int)(sizeof(CK_C13) / sizeof(CK_C13[0])) },
};
#define CHK_NCATS  ((int)(sizeof(CHK_CATS) / sizeof(CHK_CATS[0])))
#define CHK_MAXITEMS 32
#define CHK_PATH "/luma/plugins/0004000000033500/Checklist.txt"

static u8  chkState[CHK_NCATS][CHK_MAXITEMS]; // 0 untouched, 1 auto, 2 you-checked, 3 you-cleared
static int chkLoaded = 0;

// ---- type-icons: 8x8 monochrome bitmaps, scaled to any pixel size (placeholders; real sprites
// can replace these later - see oot3d_items.png "QUEST STATUS SCREEN" once extracted) ----
static const u8 iconHeartBmp[8] = { 0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00 };
static const u8 iconSkullBmp[8] = { 0x24,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x42 };
static const u8 iconNoteBmp[8]  = { 0x0C,0x0C,0x0E,0x0C,0x0C,0x7C,0xFC,0x78 };
static const u8 iconKeyBmp[8]   = { 0x38,0x44,0x44,0x38,0x10,0x10,0x34,0x00 };
static void DrawBitmapIcon(const u8 *bmp, int x, int y, int cell, u8 r, u8 g, u8 b)
{
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
            if (bmp[row] & (0x80 >> col))
                CFill(x + col * cell, y + row * cell, cell, cell, r, g, b);
}
// cell=2 -> 16px (list rows, matches DrawSprite's small size); cell=5 -> 40px (detail card).
static void DrawChkIcon(const ChkItem *it, int x, int y, int cell)
{
    switch (it->iconKind)
    {
        case CKI_SPRITE:  if (FindSprite(it->iconArg)) DrawSprite(x, y, it->iconArg, cell >= 4); break;
        case CKI_HEART:   DrawBitmapIcon(iconHeartBmp, x, y, cell, 220, 60, 60); break;
        case CKI_SKULL:   DrawBitmapIcon(iconSkullBmp, x, y, cell, 224, 186, 96); break;
        case CKI_NOTE: {
            // Each ocarina song's note has a color (per the in-game song menu). iconArg selects it:
            // 0 = cyan (the 4 non-warp songs are all cyan) then the 6 warp songs by their color.
            static const u8 songCol[7][3] = {
                { 90,210,230}, // 0 cyan  - Zelda's Lullaby / Sun / Epona / Saria
                { 90,210, 90}, // 1 green - Minuet of Forest
                {230, 90, 90}, // 2 red   - Bolero of Fire
                { 96,128,235}, // 3 blue  - Serenade of Water
                {235,160, 70}, // 4 orange- Requiem of Spirit
                {184,102,222}, // 5 purple- Nocturne of Shadow
                {232,212, 84}, // 6 yellow- Prelude of Light
            };
            int ci = it->iconArg < 7 ? it->iconArg : 0;
            DrawBitmapIcon(iconNoteBmp, x, y, cell, songCol[ci][0], songCol[ci][1], songCol[ci][2]);
            break;
        }
        case CKI_KEYITEM: DrawBitmapIcon(iconKeyBmp,   x, y, cell, 180, 150, 90); break;
        default: break;
    }
}

// Small-font clip-with-ellipsis (CTextClip's sibling for CText6). Reusable beyond the checklist.
static void CText6Clip(int x, int y, const char *s, int maxw, u8 r, u8 g, u8 b)
{
    char buf[64]; int n = 0;
    while (s[n] && n < 62) { buf[n] = s[n]; n++; }
    buf[n] = 0;
    if (C6Width(buf) <= maxw) { CText6(x, y, buf, r, g, b); return; }
    while (n > 1)
    {
        buf[--n] = 0;
        char tmp[66]; int t = 0;
        for (int i = 0; i < n; ++i) tmp[t++] = buf[i];
        tmp[t++] = '.'; tmp[t++] = '.'; tmp[t] = 0;
        if (C6Width(tmp) <= maxw) { CText6(x, y, tmp, r, g, b); return; }
    }
    CText6(x, y, "..", r, g, b);
}
// Clipped small-font draw used by the marquee to paint only inside [clipX0, clipX1).
static void CText6ClipRegion(int x, int y, const char *s, int clipX0, int clipX1, u8 r, u8 g, u8 b)
{
    while (*s)
    {
        unsigned char ch = SmallAscii(SysFontUtf8Next(&s));
        if (ch)
        {
            const unsigned char *glyph = &font[ch * FONT_HEIGHT];
            for (int dy = 0; dy < FONT_HEIGHT; ++dy)
                for (int dx = 0; dx < FONT_WIDTH; ++dx)
                    if (glyph[dy] & (0x80 >> dx))
                    {
                        int X = x + dx, Y = y + dy;
                        if (X < clipX0 || X >= clipX1) continue;
                        if ((unsigned)X < TOP_W && (unsigned)Y < TOP_H)
                        { u8 *p = CPix(X, Y); p[0] = r; p[1] = g; p[2] = b; }
                    }
        }
        x += FONT_WIDTH + 1;
    }
}
// Selected-row marquee: static (clipped) if it fits or the hold delay hasn't elapsed yet, else
// scrolls left and loops with a gap. `delay` is in frames (~16ms each) and is also the moment
// the scroll animation itself starts counting from, so it always starts smoothly at offset 0.
#define CHK_MARQUEE_DELAY      62    // ~1s - list rows
#define CHK_HINT_MARQUEE_DELAY 124   // ~2s - the item-card Hint field
#define CHK_SPEED_LIST 3, 2 // 1.5x  - list rows (task name)
#define CHK_SPEED_FAST 9, 4 // 2.25x - Hint/Where (1.5x on top of the 1.5x list speed)
static void CText6Marquee(int x, int y, int w, const char *s, int tick, int delay, int spdNum, int spdDen, u8 r, u8 g, u8 b)
{
    int tw = C6Width(s);
    if (tw <= w || tick < delay) { CText6Clip(x, y, s, w, r, g, b); return; }
    int cyclepx = tw + 24;
    int off = ((tick - delay) * spdNum / spdDen) % cyclepx;
    CText6ClipRegion(x - off, y, s, x, x + w, r, g, b);
    CText6ClipRegion(x - off + cyclepx, y, s, x, x + w, r, g, b);
}
// Wraps a short label into up to 2 lines within width w, breaking on spaces. A lone "&" is
// glued to the word that follows it BEFORE wrapping, so a line never ends on a dangling "&" -
// used for hub category names ("Zora's Domain & Jabu-Jabu" etc).
//
// Split choice is BALANCED, not greedy: if the whole label fits on one line, it stays on one
// line. Otherwise, among every point where it could break into 2 lines, pick the one that
// minimizes the longer of the two resulting line widths. A naive "pack line1 as full as
// possible" wrap strands short connector words ("an", "&") alone on line1 just because there
// happened to be room, e.g. "Becoming an" / "Adult" instead of the more even "Becoming" /
// "an Adult"; balancing avoids that.
typedef int (*ChkMeasureFn)(const char *);
static void ChkWrapBalanced(const char *s, int w, char *line1, char *line2, ChkMeasureFn measure)
{
    char buf[64]; int n = 0;
    while (s[n] && n < 62) { buf[n] = s[n]; n++; }
    buf[n] = 0;

    char *tok[12]; int ntok = 0;
    char *p = buf;
    while (*p && ntok < 12)
    {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        tok[ntok++] = p;
        while (*p && *p != ' ') p++;
    }
    char *mtok[12]; int nmtok = 0;
    for (int i = 0; i < ntok; ++i)
    {
        if (strcmp(tok[i], "&") == 0 && i + 1 < ntok)
        { *(tok[i + 1] - 1) = ' '; mtok[nmtok++] = tok[i]; ++i; } // "&" + next word -> one token
        else mtok[nmtok++] = tok[i];
    }

    line1[0] = 0; line2[0] = 0;
    if (nmtok == 0) return;

    char joined[64]; joined[0] = 0;
    for (int i = 0; i < nmtok; ++i)
    { char c[64]; if (joined[0]) sniprintf(c, sizeof c, "%s %s", joined, mtok[i]); else sniprintf(c, sizeof c, "%s", mtok[i]); strcpy(joined, c); }
    if (measure(joined) <= w) { strcpy(line1, joined); return; } // fits on one line - don't split it

    int bestSplit = -1, bestMax = 0x7FFFFFFF;
    char cur[64]; cur[0] = 0;
    for (int k = 0; k < nmtok; ++k)
    {
        char cand[64];
        if (cur[0]) sniprintf(cand, sizeof cand, "%s %s", cur, mtok[k]); else sniprintf(cand, sizeof cand, "%s", mtok[k]);
        strcpy(cur, cand);
        int w1 = measure(cur);
        if (w1 > w) break; // line1 can't extend this far and still fit
        int w2 = 0;
        if (k + 1 < nmtok)
        {
            char rest[64]; rest[0] = 0;
            for (int j = k + 1; j < nmtok; ++j)
            { char c2[64]; if (rest[0]) sniprintf(c2, sizeof c2, "%s %s", rest, mtok[j]); else sniprintf(c2, sizeof c2, "%s", mtok[j]); strcpy(rest, c2); }
            w2 = measure(rest);
        }
        int m = w1 > w2 ? w1 : w2;
        if (m < bestMax) { bestMax = m; bestSplit = k + 1; }
    }
    if (bestSplit < 0) bestSplit = 1; // even the first token alone overflows - force it anyway

    for (int i = 0; i < bestSplit; ++i)
    { char c[64]; if (line1[0]) sniprintf(c, sizeof c, "%s %s", line1, mtok[i]); else sniprintf(c, sizeof c, "%s", mtok[i]); strcpy(line1, c); }
    for (int i = bestSplit; i < nmtok; ++i)
    { char c[64]; if (line2[0]) sniprintf(c, sizeof c, "%s %s", line2, mtok[i]); else sniprintf(c, sizeof c, "%s", mtok[i]); strcpy(line2, c); }
}
#define CHK_BIGLINE_H 13 // stacked-line pitch for 2-line system-font labels (hub grid/buttons)
// Left-aligned 2-line wrap, system font, vertically centered within box height h (paired with a
// right-aligned count next to it, e.g. hub top screen). h-centering matters: a selection
// highlight covers the full row - if this always assumed 1 line, a 2-line label's 2nd line
// would spill out past the highlight instead of sitting inside it.
static void CTextWrap2(int x, int y, int w, int h, const char *s, u8 r, u8 g, u8 b)
{
    char line1[64], line2[64];
    ChkWrapBalanced(s, w, line1, line2, CTextWidth);
    int blockH = line2[0] ? (CHK_BIGLINE_H * 2) : CHK_BIGLINE_H;
    int ly = y + (h - blockH) / 2;
    CText(x, ly, line1, r, g, b, 0);
    if (line2[0]) CTextClip(x, ly + CHK_BIGLINE_H, line2, w, r, g, b, 0);
}
// 2-line wrap, system font, centered on BOTH axes inside a box (x,y,w,h) - e.g. touch buttons,
// so a 1-line name sits in the middle of the box instead of stuck to the top like a 2-line one.
static void CTextWrap2CenterBox(int x, int y, int w, int h, const char *s, u8 r, u8 g, u8 b)
{
    char line1[64], line2[64];
    ChkWrapBalanced(s, w, line1, line2, CTextWidth);
    int blockH = line2[0] ? (CHK_BIGLINE_H * 2) : CHK_BIGLINE_H;
    int ly = y + (h - blockH) / 2;
    CText(x + (w - CTextWidth(line1)) / 2, ly, line1, r, g, b, 0);
    if (line2[0])
    {
        int lw2 = CTextWidth(line2), ly2 = ly + CHK_BIGLINE_H;
        if (lw2 <= w) CText(x + (w - lw2) / 2, ly2, line2, r, g, b, 0);
        else          CTextClip(x, ly2, line2, w, r, g, b, 0);
    }
}

static int ChkFindKey(const char *key, int *outC, int *outI)
{
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
            if (strcmp(CHK_CATS[c].items[i].key, key) == 0) { *outC = c; *outI = i; return 1; }
    return 0;
}
static void ChecklistSave(void)
{
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, CHK_PATH),
                                 FS_OPEN_WRITE | FS_OPEN_CREATE, 0)))
        return;
    char buf[80]; u32 off = 0, wrote;
    static const char *hdr = "# OcarinaCTRComposer 100% Checklist. Plugin folder; survives updates.\nVER 1\n";
    FSFILE_Write(f, &wrote, off, hdr, (u32)strlen(hdr), FS_WRITE_FLUSH); off += wrote;
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
        {
            u8 s = chkState[c][i]; if (!s) continue;
            const char *tag = (s == 1) ? "A" : (s == 2) ? "M" : "S";
            int n = sniprintf(buf, sizeof buf, "STATE %s %s\n", CHK_CATS[c].items[i].key, tag);
            FSFILE_Write(f, &wrote, off, buf, (u32)n, FS_WRITE_FLUSH); off += wrote;
        }
    FSFILE_SetSize(f, off);
    FSFILE_Close(f);
}
// Wipe every item back to "not done" and rewrite Checklist.txt (bound to START on the hub).
static void ChecklistReset(void)
{
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
            chkState[c][i] = 0;
    ChecklistSave();
}
static void ChecklistLoad(void)
{
    chkLoaded = 1;
    memset(chkState, 0, sizeof(chkState));
    FsBootInit(); if (!fsReady) return;
    Handle f;
    if (R_FAILED(FSUSER_OpenFile(&f, cfgArchive, fsMakePath(PATH_ASCII, CHK_PATH), FS_OPEN_READ, 0)))
        return;
    u64 sz64 = 0; FSFILE_GetSize(f, &sz64);
    u32 sz = (u32)sz64;
    if (sz == 0 || sz > 64 * 1024) { FSFILE_Close(f); return; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { FSFILE_Close(f); return; }
    u32 got = 0;
    Result r = FSFILE_Read(f, &got, 0, buf, sz);
    FSFILE_Close(f);
    if (R_FAILED(r) || !got) { free(buf); return; }
    buf[got] = 0;
    char *p = buf;
    while (*p)
    {
        char *line = p;
        while (*p && *p != '\n') p++;
        char *eol = p; if (*p) p++;
        if (eol > line && eol[-1] == '\r') eol[-1] = 0;
        *eol = 0;
        if (!strncmp(line, "STATE ", 6))
        {
            char *key = line + 6;
            char *sp = strchr(key, ' ');
            if (sp)
            {
                *sp = 0; char *tag = sp + 1;
                int c, i;
                if (ChkFindKey(key, &c, &i))
                {
                    u8 v = (tag[0] == 'A') ? 1 : (tag[0] == 'M') ? 2 : (tag[0] == 'S') ? 3 : 0;
                    if (v) chkState[c][i] = v;
                }
            }
        }
    }
    free(buf);
}
static void ChecklistLoadOnce(void) { if (!chkLoaded) ChecklistLoad(); }

// Auto-fill = SYNC the auto-marks to the CURRENTLY loaded save (so switching to a lesser save no
// longer keeps a previous save's marks). For each item it reads live gSaveContext:
//   detected  -> set state 1 (auto), but only from 0/3 - never overrides 2 (you manually checked)
//   NOT detected -> clear state 1 (stale auto) back to 0, but leave 2/3 (your own tracking) alone
// Detections are false-positive-proof, so re-marking a 3 (you-cleared) that the save confirms is safe.
// g_afAdd / g_afRem hold the last run's added / removed counts for the on-screen result.
static int g_afAdd, g_afRem;
static int ChecklistAutoFill(void)
{
    g_afAdd = g_afRem = 0;
    for (int c = 0; c < CHK_NCATS; ++c)
        for (int i = 0; i < CHK_CATS[c].count; ++i)
        {
            const ChkItem *it = &CHK_CATS[c].items[i];
            int got = 0;
            if (it->kind == CK_EQUIP)          got = (R8(it->addr) & it->mask) != 0;
            else if (it->kind == CK_QUESTALL)  got = R8(it->addr) == it->mask;
            else if (it->kind == CK_INVSLOT)   got = it->mask ? (R8(it->addr) == it->mask)
                                                              : (R8(it->addr) != 0xFF);
            else if (it->kind == CK_UPGRADE)   got = ((R32(it->addr) >> (it->mask >> 3)) & 0x7)
                                                     >= (it->mask & 0x7);
            else continue; // not an auto-detectable item -> never auto-touch it
            u8 st = chkState[c][i];
            if (got) { if (st == 0 || st == 3) { chkState[c][i] = 1; ++g_afAdd; } }
            else     { if (st == 1)            { chkState[c][i] = 0; ++g_afRem; } }
        }
    return g_afAdd + g_afRem;
}
// Build the "+A -R" / "Up to date" result string for the auto-fill button.
static void ChkAfMsg(char *buf, const char **msg)
{
    if (g_afAdd || g_afRem) { siprintf(buf, "+%d -%d", g_afAdd, g_afRem); *msg = buf; }
    else *msg = "Up to date";
}

// Bottom screen is 320px wide, NOT the top window's 360 - every x below is native to that.
#define CHKB_L    8
#define CHKB_R    312
#define CHKB_COLW 148
// Hub grid is paginated 2 cols x 4 rows (8 categories/page) - a flat unpaginated grid stopped
// fitting once the dataset grew past a couple of test areas, and 2-line names need more row
// height than the old 1-line design had room for.
#define HUB_COLS   2
#define HUB_ROWS   4
#define HUB_PAGESZ (HUB_COLS * HUB_ROWS)
#define CHKB_BTN_H  36
#define CHKB_STRIDE 40
#define CHKB_TOP    10
// Marquee text-box widths, mirrored from the draw code below (task row / hint & where pill) -
// duplicated here so the pre-draw "is anything actually scrolling" check can be computed
// without doing a full draw pass.
#define CHK_TASK_TXTW ((CHKB_R - 16) - 6 - (CHKB_L + 16 + 8))
#define CHK_HINT_SVW  (WIN_X + WIN_W - 12 - (WIN_X + 86))
static void ToolChecklist(void)
{
    static int level = 0;             // 0 hub, 1 item list (persists across SELECT/reopen)
    static int catCur = 0;
    static int catCursor[CHK_NCATS], catScroll[CHK_NCATS];
    static int filterMode = 0;        // 0 All, 1 Todo, 2 Done
    static int revealLoc = 0;         // X toggle, sticky for the session
    static int selTick = 0;           // frames on the current item -> drives its marquee delay
    static const char *afMsg = NULL;  // Auto-fill result shown on the button (right side), timed
    static int afTick = 0;            // countdown for afMsg
    static char afBuf[24];
    ChecklistLoadOnce();
    if (cheatState[CH_CFG_AUTOFILL]) // auto-fill from the save every time the Checklist opens (Settings toggle)
    {
        ChecklistAutoFill(); ChecklistSave(); ChkAfMsg(afBuf, &afMsg);
        afTick = 180;
    }
    KbInit(); // touch service - every other touch-using tool calls this too

    int touchPrev = HidTouch(0, 0);
    u32 prev = HID_PAD;
    int redraw = 1;
    int lastCur = -1000;
    int hubPages = (CHK_NCATS + HUB_PAGESZ - 1) / HUB_PAGESZ;

    while (1)
    {
        int px, py, nowT = HidTouch(&px, &py);
        int tap = nowT && !touchPrev; touchPrev = nowT;
        if (!hidReady) tap = 0;
        svcSleepThread(16 * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);
        int selOverflow = 0; // is a marquee on the CURRENT item actually mid-scroll right now?

        if (down & BUTTON_SELECT) { g_quitToGame = 1; return; } // resumes at this level/cat/cursor

        if (level == 0)
        {
            if (down & BUTTON_B) return;
            int page = catCur / HUB_PAGESZ, local = catCur % HUB_PAGESZ;
            int pageBase = page * HUB_PAGESZ;
            int pageN = CHK_NCATS - pageBase; if (pageN > HUB_PAGESZ) pageN = HUB_PAGESZ;
            int pageRows = (pageN + HUB_COLS - 1) / HUB_COLS;
            int col = local % HUB_COLS, row = local / HUB_COLS;
            if (down & BUTTON_DOWN)
            { row = (row + 1) % pageRows; int ni = col + row * HUB_COLS;
              catCur = pageBase + ((ni < pageN) ? ni : (row * HUB_COLS < pageN ? row * HUB_COLS : pageN - 1)); redraw = 1; }
            if (down & BUTTON_UP)
            { row = (row - 1 + pageRows) % pageRows; int ni = col + row * HUB_COLS;
              catCur = pageBase + ((ni < pageN) ? ni : (row * HUB_COLS < pageN ? row * HUB_COLS : pageN - 1)); redraw = 1; }
            if (down & (BUTTON_LEFT | BUTTON_RIGHT))
            { int nc = (1 - col) + row * HUB_COLS; if (nc < pageN) catCur = pageBase + nc; redraw = 1; }
            if ((down & BUTTON_L1) && hubPages > 1)
            { int np = (page - 1 + hubPages) % hubPages, npBase = np * HUB_PAGESZ, npN = CHK_NCATS - npBase; if (npN > HUB_PAGESZ) npN = HUB_PAGESZ;
              catCur = npBase + (local < npN ? local : npN - 1); redraw = 1; }
            if ((down & BUTTON_R1) && hubPages > 1)
            { int np = (page + 1) % hubPages, npBase = np * HUB_PAGESZ, npN = CHK_NCATS - npBase; if (npN > HUB_PAGESZ) npN = HUB_PAGESZ;
              catCur = npBase + (local < npN ? local : npN - 1); redraw = 1; }
            if (tap)
            {
                for (int li = 0; li < pageN; ++li)
                {
                    int c2 = li % HUB_COLS, r2 = li / HUB_COLS, bx = CHKB_L + c2 * (CHKB_COLW + 8), by = CHKB_TOP + r2 * CHKB_STRIDE;
                    if (px >= bx && px < bx + CHKB_COLW && py >= by && py < by + CHKB_BTN_H)
                    { catCur = pageBase + li; level = 1; catCursor[pageBase + li] = 0; catScroll[pageBase + li] = 0; lastCur = -1000; redraw = 1; }
                }
                int gridBottom = CHKB_TOP + HUB_ROWS * CHKB_STRIDE - (CHKB_STRIDE - CHKB_BTN_H);
                int fy = gridBottom + 20;
                if (px >= CHKB_L && px < CHKB_R && py >= fy && py < fy + 24)
                { ChecklistAutoFill(); ChecklistSave(); ChkAfMsg(afBuf, &afMsg);
                  afTick = 180; redraw = 1; }
            }
            if (down & BUTTON_Y) { ChecklistAutoFill(); ChecklistSave(); ChkAfMsg(afBuf, &afMsg);
                afTick = 180; redraw = 1; }
            if (down & BUTTON_START) { ChecklistReset(); afMsg = "Reset"; afTick = 180; lastCur = -1000; redraw = 1; }
            if (down & BUTTON_A) { level = 1; catCursor[catCur] = 0; catScroll[catCur] = 0; lastCur = -1000; redraw = 1; }
            if (afTick > 0 && --afTick == 0) { afMsg = NULL; redraw = 1; } // clear the button result on timeout
        }
        else
        {
            const ChkCat *cc = &CHK_CATS[catCur];
            int cursor = catCursor[catCur], scroll = catScroll[catCur];

            // filtered index list for this category
            int filtIdx[CHK_MAXITEMS], filtN = 0;
            for (int i = 0; i < cc->count; ++i)
            {
                int checked = (chkState[catCur][i] == 1 || chkState[catCur][i] == 2);
                if (filterMode == 1 && checked) continue;
                if (filterMode == 2 && !checked) continue;
                filtIdx[filtN++] = i;
            }
            if (cursor >= filtN) cursor = filtN > 0 ? filtN - 1 : 0;

            if (down & BUTTON_B) { level = 0; redraw = 1; }
            if (down & BUTTON_L1) { catCur = (catCur + CHK_NCATS - 1) % CHK_NCATS; lastCur = -1000; redraw = 1; }
            if (down & BUTTON_R1) { catCur = (catCur + 1) % CHK_NCATS; lastCur = -1000; redraw = 1; }
            if (down & BUTTON_Y)  { filterMode = (filterMode + 1) % 3; redraw = 1; }
            if (down & BUTTON_X)  { revealLoc = !revealLoc; redraw = 1; }
            if (down & BUTTON_DOWN && filtN > 0) { cursor = (cursor + 1) % filtN; redraw = 1; }
            if (down & BUTTON_UP   && filtN > 0) { cursor = (cursor - 1 + filtN) % filtN; redraw = 1; }
            // D-Pad Left/Right page through the item list (8 rows visible); shoulders stay area-switch
            if (down & BUTTON_RIGHT && filtN > 0) { cursor += 8; if (cursor >= filtN) cursor = filtN - 1; redraw = 1; }
            if (down & BUTTON_LEFT  && filtN > 0) { cursor -= 8; if (cursor < 0) cursor = 0; redraw = 1; }

            if (tap && filtN > 0)
            {
                for (int r = 0; r < 8 && scroll + r < filtN; ++r)
                    if (px >= CHKB_L && px < CHKB_R && py >= 34 + r * 18 && py < 34 + r * 18 + 18)
                    { cursor = scroll + r; redraw = 1; }
            }
            if ((down & BUTTON_A) && filtN > 0)
            {
                int gi = filtIdx[cursor];
                int checked = (chkState[catCur][gi] == 1 || chkState[catCur][gi] == 2);
                chkState[catCur][gi] = (u8)(checked ? 3 : 2);
                ChecklistSave(); redraw = 1;
            }

            catCursor[catCur] = cursor; catScroll[catCur] = scroll;
            if (cursor != lastCur) { selTick = 0; lastCur = cursor; }
            else if (selTick < 100000) selTick++;

            // Mirrors CText6Marquee's own "tw > w && tick >= delay" trigger for each of the 3
            // marqueed fields on the current item, WITHOUT drawing anything - lets the redraw
            // gate below stay cheap (skip drawing) until a marquee is genuinely about to scroll,
            // instead of flipping to "redraw every tick" as soon as any delay elapses regardless
            // of whether that field even needs to scroll. Getting this wrong is exactly what
            // made the hint's 2s delay measure as ~10s before: once selTick passed the list-row
            // delay (62 ticks, ~1s), every tick started paying the ~100ms+ full-redraw cost even
            // though the hint field's own 124-tick delay hadn't elapsed yet - so the hint delay
            // was actually being counted in ~100ms+ ticks instead of ~16ms ones.
            if (filtN > 0)
            {
                const ChkItem *sit = &cc->items[filtIdx[cursor]];
                if (selTick >= CHK_MARQUEE_DELAY      && C6Width(sit->task) > CHK_TASK_TXTW) selOverflow = 1;
                if (selTick >= CHK_HINT_MARQUEE_DELAY && C6Width(sit->hint) > CHK_HINT_SVW)   selOverflow = 1;
                if (revealLoc && sit->loc[0] && selTick >= CHK_MARQUEE_DELAY && C6Width(sit->loc) > CHK_HINT_SVW - 6) selOverflow = 1;
            }
        }

        // A full redraw here means re-compositing the whole backdrop + bottom-screen frame -
        // expensive on this hardware (~100ms+). Only pay that cost when something actually
        // changed (redraw==1) or a marquee is genuinely mid-scroll (tick past its delay). While
        // just WAITING for a delay to elapse, nothing on screen changes, so we skip the redraw
        // entirely and let the ~16ms sleep above be the only per-tick cost - that's what keeps
        // the 1s/2s delays accurate; redrawing every tick during the wait was inflating each
        // "tick" to ~127ms, which is exactly why the delay measured ~8x too long.
        int animating = (level == 1) && selOverflow; // hub has no marquee anymore
        if (!redraw && !animating) continue;
        redraw = 0;

        // =========================================================== DRAW
        if (level == 0)
        {
            int page = catCur / HUB_PAGESZ, pageBase = page * HUB_PAGESZ;
            int pageN = CHK_NCATS - pageBase; if (pageN > HUB_PAGESZ) pageN = HUB_PAGESZ;

            ComposeBackdrop();
            CText(WIN_X + 12, WIN_Y + 7, T("Checklist 100%"), INK, 1);
            CFill(WIN_X + 12, WIN_Y + 24, CTextWidth(T("Checklist 100%")) + 6, 1, GOLD);
            if (hubPages > 1)
            {
                char pg[16]; siprintf(pg, "%d/%d", page + 1, hubPages);
                CText6(WIN_X + WIN_W - 12 - C6Width(pg), WIN_Y + 9, pg, INK_DIM);
            }
            int totalDone = 0, totalAll = 0;
            for (int c = 0; c < CHK_NCATS; ++c) // totals always cover ALL categories, not just this page
            {
                int done = 0;
                for (int i = 0; i < CHK_CATS[c].count; ++i)
                    if (chkState[c][i] == 1 || chkState[c][i] == 2) ++done;
                totalDone += done; totalAll += CHK_CATS[c].count;
            }
            int colW = ROW_W / 2, gridY = WIN_Y + 30, rowH = 28, rowGap = 3;
            for (int li = 0; li < pageN; ++li)
            {
                int c = pageBase + li;
                int done = 0;
                for (int i = 0; i < CHK_CATS[c].count; ++i)
                    if (chkState[c][i] == 1 || chkState[c][i] == 2) ++done;
                int col = li % HUB_COLS, row = li / HUB_COLS;
                int x = ROW_X + col * colW, y = gridY + row * (rowH + rowGap);
                int full = (CHK_CATS[c].count > 0 && done == CHK_CATS[c].count);
                int selc = (c == catCur);
                const u8 *nc = full ? CGREEN : (selc ? CGOLD : CINK);
                if (selc) CFillBlend(x - 3, y, colW - 6, rowH, 0, 0, 0, 110);
                char frac[16]; siprintf(frac, "%d/%d", done, CHK_CATS[c].count);
                int fw = C6Width(frac);
                int nameW = colW - fw - 16;
                CTextWrap2(x, y, nameW, rowH, CHK_CATS[c].name, nc[0], nc[1], nc[2]);
                CText6(x + colW - 12 - fw, y + (rowH - FONT_HEIGHT) / 2, frac, nc[0], nc[1], nc[2]);
            }
            int ty = gridY + HUB_ROWS * (rowH + rowGap) + 4;
            CFill(WIN_X + 12, ty, WIN_W - 24, 1, GOLD);
            CText6(WIN_X + 12, ty + 8, "Total", INK);
            char totFrac[24]; int pct = totalAll > 0 ? totalDone * 100 / totalAll : 0;
            siprintf(totFrac, "%d/%d (%d%%)", totalDone, totalAll, pct);
            CText6(WIN_X + WIN_W - 12 - C6Width(totFrac), ty + 8, totFrac, GREEN_ON);
            CFill(WIN_X + 12, ty + 20, WIN_W - 24, 5, 26, 20, 12);
            int barw = totalAll > 0 ? (WIN_W - 24) * totalDone / totalAll : 0;
            CFill(WIN_X + 12, ty + 20, barw, 5, GREEN_ON);
            int lx = WIN_X + 12, ly = ty + 31;
            const char *chips[4] = { "auto", "you", "todo", "cleared" };
            const u8 *chipc[4] = { CGREEN, CGOLD, CDIM, CDIM };
            for (int i = 0; i < 4; ++i)
            {
                CFill(lx, ly, 8, 8, chipc[i][0], chipc[i][1], chipc[i][2]);
                CText6(lx + 12, ly, chips[i], INK_DIM);
                lx += 12 + C6Width(chips[i]) + 6;
            }
            Present(); Present();

            for (int y = 0; y < BOT_H; ++y)
                for (int x = 0; x < BOT_W; ++x)
                {
                    u8 *p = CPix(x, y);
                    if (savedBotValid)
                    { u16 v = savedBot[y * BOT_W + x];
                      p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
                    else { p[0] = p[1] = p[2] = 12; }
                }
            CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
            CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
            for (int li = 0; li < pageN; ++li)
            {
                int i = pageBase + li;
                int col = li % HUB_COLS, row = li / HUB_COLS, bx = CHKB_L + col * (CHKB_COLW + 8), by = CHKB_TOP + row * CHKB_STRIDE;
                int done = 0;
                for (int k = 0; k < CHK_CATS[i].count; ++k)
                    if (chkState[i][k] == 1 || chkState[i][k] == 2) ++done;
                int full = (done == CHK_CATS[i].count);
                int sel = (i == catCur);
                const u8 *bc = full ? CGREEN : CGOLD;
                CFill(bx, by, CHKB_COLW, CHKB_BTN_H, sel ? 52 : 32, sel ? 44 : 25, sel ? 26 : 16);
                CFill(bx, by, CHKB_COLW, 1, bc[0], bc[1], bc[2]); CFill(bx, by + CHKB_BTN_H - 1, CHKB_COLW, 1, bc[0], bc[1], bc[2]);
                CFill(bx, by, 1, CHKB_BTN_H, bc[0], bc[1], bc[2]); CFill(bx + CHKB_COLW - 1, by, 1, CHKB_BTN_H, bc[0], bc[1], bc[2]);
                if (sel) CFill(bx, by, 4, CHKB_BTN_H, 255, 255, 255); // bright left bar - unmistakably "selected"
                // No fraction here - the top screen already shows X/Y per category, repeating it
                // on the touch buttons just stole width from the name and forced truncation.
                CTextWrap2CenterBox(bx + 6, by, CHKB_COLW - 12, CHKB_BTN_H, CHK_CATS[i].name, bc[0], bc[1], bc[2]);
            }
            int gridBottom = CHKB_TOP + HUB_ROWS * CHKB_STRIDE - (CHKB_STRIDE - CHKB_BTN_H);
            // above the Auto-fill box: navigation hints, horizontally centered
            const char *hUp = hubPages > 1 ? T("{DP} move   {A} open   {L}/{R} areas") : T("{DP} move   {A} open");
            CText6Btn((BOT_W - C6BtnWidth(hUp)) / 2, gridBottom + 6, hUp, INK_DIM);
            int fy = gridBottom + 20;
            CFill(CHKB_L, fy, CHKB_R - CHKB_L, 24, 20, 40, 20);
            CFill(CHKB_L, fy, CHKB_R - CHKB_L, 1, GREEN_ON); CFill(CHKB_L, fy + 23, CHKB_R - CHKB_L, 1, GREEN_ON);
            CFill(CHKB_L, fy, 1, 24, GREEN_ON); CFill(CHKB_R - 1, fy, 1, 24, GREEN_ON);
            int atw = C6Width(T("Auto-fill from save"));
            CText6(CHKB_L + (CHKB_R - CHKB_L - atw) / 2, fy + 7, T("Auto-fill from save"), GREEN_ON);
            if (afMsg) // on-screen result, right side of the button (no need to leave the screen)
            {
                const char *m = T(afMsg);
                if (afMsg[0] == 'N' || afMsg[0] == 'R') CText6(CHKB_R - 8 - C6Width(m), fy + 7, m, INK_DIM); // Nothing new / Reset
                else                                    CText6(CHKB_R - 8 - C6Width(m), fy + 7, m, 255, 236, 120); // OK: +N
            }
            // below the box: action hints, horizontally centered (START has no glyph, shown as text)
            const char *hDn = T("{Y} Auto-fill    START reset");
            CText6Btn((BOT_W - C6BtnWidth(hDn)) / 2, fy + 30, hDn, INK_DIM);
            BotBlitComposeBoth();
        }
        else
        {
            const ChkCat *cc = &CHK_CATS[catCur];
            int cursor = catCursor[catCur], scroll = catScroll[catCur];
            int filtIdx[CHK_MAXITEMS], filtN = 0;
            for (int i = 0; i < cc->count; ++i)
            {
                int checked = (chkState[catCur][i] == 1 || chkState[catCur][i] == 2);
                if (filterMode == 1 && checked) continue;
                if (filterMode == 2 && !checked) continue;
                filtIdx[filtN++] = i;
            }
            if (cursor >= filtN) cursor = filtN > 0 ? filtN - 1 : 0;
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + 8) scroll = cursor - 7;
            catCursor[catCur] = cursor; catScroll[catCur] = scroll;

            ComposeBackdrop();
            CTextClip(WIN_X + 12, WIN_Y + 7, cc->name, 200, INK, 1);
            int done = 0; for (int i = 0; i < cc->count; ++i) if (chkState[catCur][i]==1||chkState[catCur][i]==2) ++done;
            char hdr[16]; siprintf(hdr, "%d/%d", done, cc->count);
            CText6(WIN_X + WIN_W - 12 - C6Width(hdr), WIN_Y + 9, hdr, INK_DIM);
            CFill(WIN_X + 12, WIN_Y + 22, WIN_W - 24, 1, GOLD);

            if (filtN > 0)
            {
                int gi = filtIdx[cursor];
                const ChkItem *it = &cc->items[gi];
                int bx = WIN_X + 12, by = WIN_Y + 34;
                CFill(bx, by, 66, 66, 26, 20, 12); CFill(bx, by, 66, 1, GOLD); CFill(bx, by+65, 66, 1, GOLD);
                CFill(bx, by, 1, 66, GOLD); CFill(bx+65, by, 1, 66, GOLD);
                DrawChkIcon(it, bx + 13, by + 13, 5); // 40px icon, centered in the 66px box
                int tx = bx + 76;
                CTextClip(tx, by, it->task, WIN_X + WIN_W - 12 - tx, GOLD, 0);
                u8 st = chkState[catCur][gi];
                int fy2 = by + 72;
                CFill(WIN_X + 12, fy2, WIN_W - 24, 1, 120, 98, 50); // hairline
                int ly2 = fy2 + 10;
                CText6(WIN_X + 12, ly2, T("Status"), GOLD);
                const char *statTxt = st == 1 ? T("From your save") : st == 2 ? T("Checked by you")
                                     : st == 3 ? T("You cleared this") : T("Not done yet");
                const u8 *statC = st == 1 ? CGREEN : st == 2 ? CGOLD : st == 3 ? CGOLD : CDIM;
                u8 sc[3]; LiftForDark(statC[0], statC[1], statC[2], sc); // keep legible on the dark pill (dark-accent themes)
                int svx = WIN_X + 86, svw = WIN_X + WIN_W - 12 - svx;
                CFill(svx, ly2 - 2, svw, 14, 20, 16, 10);
                CFill(svx, ly2 - 2, svw, 1, sc[0], sc[1], sc[2]); CFill(svx, ly2+11, svw, 1, sc[0], sc[1], sc[2]);
                CText6Clip(svx + (svw - C6Width(statTxt)) / 2, ly2, statTxt, svw - 6, sc[0], sc[1], sc[2]);

                int hy = ly2 + 24;
                CText6(WIN_X + 12, hy, T("Hint"), GOLD);
                CText6Marquee(svx, hy, svw, it->hint, selTick, CHK_HINT_MARQUEE_DELAY, CHK_SPEED_FAST, 236, 236, 210);

                int oy = hy + 20;
                CText6(WIN_X + 12, oy, T("Where"), GOLD);
                if (!it->loc[0]) CText6(svx, oy, "-", 140, 130, 104);
                else if (!revealLoc)
                {
                    CFill(svx, oy - 2, svw, 14, 20, 16, 10);
                    CFill(svx, oy - 2, svw, 1, GREEN_ON); CFill(svx, oy+11, svw, 1, GREEN_ON);
                    const char *lk = T("X: show location");
                    CText6Clip(svx + (svw - C6Width(lk)) / 2, oy, lk, svw - 6, GREEN_ON);
                }
                else
                {
                    CFill(svx, oy - 2, svw, 14, 20, 16, 10);
                    CFill(svx, oy - 2, svw, 1, GREEN_ON); CFill(svx, oy+11, svw, 1, GREEN_ON);
                    CText6Marquee(svx + 3, oy, svw - 6, it->loc, selTick, CHK_MARQUEE_DELAY, CHK_SPEED_FAST, GREEN_ON);
                }
            }
            else CText6Clip(WIN_X + 12, WIN_Y + 100, T("Nothing here - try Y to change the filter."), WIN_W - 24, INK_DIM);
            CText6Btn(WIN_X + 12, WIN_Y + WIN_H - 14, T("{L}/{R} area   {X} location"), INK_DIM);
            Present(); Present();

            for (int y = 0; y < BOT_H; ++y)
                for (int x = 0; x < BOT_W; ++x)
                {
                    u8 *p = CPix(x, y);
                    if (savedBotValid)
                    { u16 v = savedBot[y * BOT_W + x];
                      p[0] = (u8)(((v>>11)&31)<<3); p[1] = (u8)(((v>>5)&63)<<2); p[2] = (u8)((v&31)<<3); }
                    else { p[0] = p[1] = p[2] = 12; }
                }
            CFillBlend(0, 0, BOT_W, BOT_H, BG, 230);
            CFill(6, 4, BOT_W - 12, 1, GOLD); CFill(6, BOT_H - 6, BOT_W - 12, 1, GOLD);
            const char *fl = filterMode == 0 ? "All" : filterMode == 1 ? "Todo" : "Done";
            char flbuf[24]; sniprintf(flbuf, sizeof flbuf, "%s %d/%d", fl, done, cc->count);
            CText6(CHKB_L, 8, "<", INK_DIM);
            CTextClip(20, 6, cc->name, 190, GOLD, 0);
            CText6(CHKB_R - 8 - C6Width(flbuf) - 10, 8, flbuf, INK_DIM);
            CText6(CHKB_R - 8, 8, ">", INK_DIM);
            CFill(CHKB_L, 26, CHKB_R - CHKB_L, 1, GOLD);
            for (int r = 0; r < 8 && scroll + r < filtN; ++r)
            {
                int gi = filtIdx[scroll + r]; int y = 34 + r * 18;
                int isCur = (scroll + r == cursor);
                if (isCur) CFillBlend(CHKB_L, y - 1, CHKB_R - CHKB_L, 18, 0, 0, 0, 110);
                const ChkItem *it = &cc->items[gi];
                DrawChkIcon(it, CHKB_L, y + 1, 2);
                u8 st = chkState[catCur][gi];
                int tx = CHKB_L + 16 + 8; // icon is 16px wide - 8px gap so text doesn't look glued to it
                int sx = CHKB_R - 16, sy = y + 3, txtw = sx - 6 - tx;
                if (isCur) CText6Marquee(tx, y + 4, txtw, it->task, selTick, CHK_MARQUEE_DELAY, CHK_SPEED_LIST, 236, 200, 120);
                else       CText6Clip(tx, y + 4, it->task, txtw, 236, 236, 210);
                if (st == 1) CFill(sx, sy, 9, 9, GREEN_ON);
                else if (st == 2) CFill(sx, sy, 9, 9, GOLD);
                else { CFill(sx, sy, 9, 1, 140,130,104); CFill(sx, sy+8, 9, 1, 140,130,104);
                       CFill(sx, sy, 1, 9, st==3?230:140, st==3?200:130, st==3?90:104);
                       CFill(sx+8, sy, 1, 9, st==3?230:140, st==3?200:130, st==3?90:104); }
            }
            if (filtN == 0) CText6(140, 100, T("Nothing here."), INK_DIM);
            // scroll arrows (same gold triangles the main menu uses): more items above/below
            int sax = (CHKB_L + CHKB_R) / 2;
            if (scroll > 0)
                for (int i = 0; i < 4; ++i) { int w = 1 + 2 * i; CFill(sax - w / 2, 28 + i, w, 1, GOLD); }
            if (scroll + 8 < filtN)
                for (int i = 0; i < 4; ++i) { int w = 7 - 2 * i; CFill(sax - w / 2, 190 + i, w, 1, GOLD); }
            { const char *hf = T("{DP} move   {A} mark/clear   {Y} filter");
              CText6Btn((BOT_W - C6BtnWidth(hf)) / 2, 210, hf, INK_DIM); }
            BotBlitComposeBoth();
        }
    }
}

static void ToolRun(int t)
{
    if (t == T_SEARCH) ToolSearch();
    else if (t == T_RAMDUMP) ToolRamDump();
    else if (t == T_HEXEDIT) ToolHexEdit();
    else if (t == T_ABOUT) ToolAbout();
    else if (t == T_GAMEGUIDE) ToolGameGuide();
    else if (t == T_PLUGINGUIDE) ToolPluginGuide();
    else if (t == T_CHECKLIST) ToolChecklist();
}

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

// ===================== Thread / entry =====================
void ThreadMain(void *arg)
{
    (void)arg; // the 3gx loader passes no argument; the signature is fixed by svcCreateThread
    InitThreadVars(); // must run before any newlib/hid/fs call on this thread

    // Make the whole game process RWX up front, exactly like CTRPluginFramework does at init.
    // Many ported cheats write to the game's read-only segments (const tables in .rodata, code in
    // .text). Under CTRPF those writes worked *only* because CTRPF had already flipped the process
    // to RWX globally; a raw plugin defaults to RO there, so those writes silently no-op. Doing the
    // same flip once here restores the CTRPF behavior for every cheat at once (Can Use All Items,
    // and any other .rodata/.text write), instead of patching RWX in one cheat at a time. This can
    // only ENABLE previously-failing writes to RO pages - writes to already-writable RAM are
    // unaffected - so it never breaks a cheat that already worked.
    svcControlProcess(CUR_PROCESS_HANDLE, PROCESSOP_SET_MMU_TO_RWX, 0, 0);

    gCompose = (u8 *)malloc(TOP_W * TOP_H * 3);
    savedBot = (u16 *)malloc(BOT_W * BOT_H * 2);
    savedTop = (u16 *)malloc(TOP_W * TOP_H * 2);
    ConfigLoad(); // restore toast toggle + quick-menu hotkey + theme + language from SD
    FavLoad();    // restore favorites (own label-keyed file, survives cheat-list changes)
    WpLoad();     // restore saved waypoints (Waypoints.dat)

    u32 prev = HID_PAD;
    int comboPrev = 0;
    while (1)
    {
        // 4ms while a toast is on screen (fast re-stamp), 20ms otherwise
        svcSleepThread((toastTicks > 0 ? 4 : 20) * 1000 * 1000);
        u32 pad = HID_PAD, down = ARepeat(pad, &prev, &g_arHold);

        const QmCombo *qc = &qmCombos[qmCombo];
        int comboNow = (pad & qc->pad) == qc->pad;

        if (comboNow && !comboPrev)
        {
            QuickMenu();
            prev = HID_PAD;
            comboNow = 1; // treat as still held: no instant reopen
            if (g_openFolder >= 0) // a folder shortcut was picked in the quick menu -> open it
            {
                int fld = g_openFolder; g_openFolder = -1;
                // Save the normal menu position and restore it afterwards, so this transient jump
                // doesn't hijack where SELECT reopens (else SELECT would keep landing in this folder).
                int sD = menuDepth, sF = menuFolder, sC = menuCursor, sS = menuScroll;
                menuDepth = 0; menuFolder = fld; menuScroll = 0; menuCursor = 0;
                { const Folder *nf = &folders[fld]; // land on the first selectable row
                  while (menuCursor < nf->count && IS_SEP(&nf->items[menuCursor])) menuCursor++;
                  if (menuCursor >= nf->count) menuCursor = 0; }
                g_qmHandoff = 1;   // the quick menu is still on screen: don't recapture it as backdrop
                RunMenu();
                menuDepth = sD; menuFolder = sF; menuCursor = sC; menuScroll = sS;
                prev = HID_PAD;
            }
            else if (g_openTool >= 0) // a tool shortcut was picked in the quick menu -> launch it
            {
                int t = g_openTool; g_openTool = -1;
                int sD = menuDepth, sF = menuFolder, sC = menuCursor, sS = menuScroll;
                g_resumeTool = t;                    // RunMenu's resume path runs the tool with full setup
                menuDepth = 0; menuFolder = 0; menuScroll = 0; menuCursor = 0; // land on HOME after the tool
                { const Folder *nf = &folders[F_ROOT]; // first selectable row, not a separator
                  while (menuCursor < nf->count && IS_SEP(&nf->items[menuCursor])) menuCursor++;
                  if (menuCursor >= nf->count) menuCursor = 0; }
                g_qmHandoff = 1;   // the quick menu is still on screen: don't recapture it as backdrop
                RunMenu();
                menuDepth = sD; menuFolder = sF; menuCursor = sC; menuScroll = sS;
                prev = HID_PAD;
            }
        }
        else if ((down & BUTTON_SELECT) && !comboNow)
        {
            RunMenu();
            prev = HID_PAD;
        }
        comboPrev = comboNow;

        ApplyCheats();
        ToastTick();
    }
}

// Normally provided by the 3dsx crt0; NULL = "no homebrew env, use real srv"
void *__service_ptr = NULL;

extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;
u32 __ctru_heap_size = 0;
u32 __ctru_linear_heap_size = 0;

void __system_allocateHeaps(PluginHeader *header)
{
    __ctru_heap_size = header->heapSize;
    __ctru_heap = header->heapVA;
    fake_heap_start = (char *)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}

void main(void)
{
    PluginHeader *header = (PluginHeader *)0x07000000;
    if (header->magic != HeaderMagic) return;
    __system_allocateHeaps(header);
    cheatState[CH_CFG_TOAST] = 1;    // notifications on by default
    cheatState[CH_CFG_AUTOFILL] = 1; // auto-fill the Checklist on open by default
    srvInit();
    plgLdrInit();
    svcControlProcess(CUR_PROCESS_HANDLE, PROCESSOP_GET_ON_EXIT_EVENT, (u32)&onProcessExitEvent, (u32)&resumeExitEvent);
    svcCreateThread(&thread, ThreadMain, 0, (u32 *)(stack + PLG_STACK_SIZE), 30, -1);
}
