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
