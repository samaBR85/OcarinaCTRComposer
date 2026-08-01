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

