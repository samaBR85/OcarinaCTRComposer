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
