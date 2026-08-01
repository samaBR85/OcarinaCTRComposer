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
