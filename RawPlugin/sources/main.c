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

#include "plugin/identity.inc.c"

#include "engine/platform.inc.c"

#include "engine/render.inc.c"

#include "plugin/cheat_ids.inc.c"
#include "engine/storage.inc.c"
#include "engine/input.inc.c"

#include "plugin/cheats.inc.c"

#include "plugin/pickers.inc.c"
// ===================== Menu model (folders) =====================
#include "engine/menu_model.inc.c"

#include "plugin/menu_tables.inc.c"

#include "engine/menu_nav.inc.c"

#include "engine/favorites.inc.c"

#include "plugin/waypoints.inc.c"

#include "engine/theme.inc.c"

#include "plugin/item_sprites.inc.c"

#include "engine/bottom_screen.inc.c"
#include "engine/toast.inc.c"

#include "engine/menu_render.inc.c"

#include "engine/tools.inc.c"

// ======================= Game Guide / Plugin Guide =======================
#include "engine/guide_reader.inc.c"

#include "plugin/guide_credits.inc.c"

#include "engine/guide_game.inc.c"

#include "plugin/plugin_guide_pages.inc.c"

#include "engine/guide_accessors.inc.c"

#include "engine/guide_plugin.inc.c"

#include "engine/tracker.inc.c"

#include "plugin/tracker_data.inc.c"

#include "engine/tracker_ui.inc.c"

#include "engine/tool_dispatch.inc.c"

#include "engine/menu_loop.inc.c"

#include "engine/quick_menu.inc.c"

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
