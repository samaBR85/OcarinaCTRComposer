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
