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

