# Changelog — OcarinaCTRComposer (Zelda: Ocarina of Time 3D)

A self-rendered `.3gx` overlay plugin for The Legend of Zelda: Ocarina of Time 3D
(New 3DS · **USA**, Title ID `0004000000033500`). Version numbers follow SemVer; the **build**
counter is the running iteration count shown on-screen (`bNN`) — it never resets, so it doubles
as the record of how many iterations went into each release.

---

## Unreleased · builds 91–171

A long polish + content pass on top of the build-90 baseline.

### Engine fixes (build 171)

Four defects inherited from the CTRComposer engine — present since its first version, so they
affect every plugin built on it. All four diagnosed from real hardware use and confirmed on
console. Warnings (`-Wall -Wextra`) were switched on in the process, which surfaced a fifth.

- **Fixed the top screen freezing when you leave the plugin.** `Present()` flips the LCD's
  buffer-select register to show our frame, and nothing ever flipped it back — so on exit the LCD
  kept scanning out the plugin's last frame while the game genuinely resumed underneath (bottom
  screen returned, audio played, top stayed frozen). The plugin now saves that register on take-over
  and restores it before handing control back. Reachable by starring a **tool** and launching it
  from the quick menu.
- **Fixed the quick menu becoming the menu's backdrop.** Coming from the quick menu, the backdrop
  grab ran microseconds after resume — before the game had drawn anything — so it captured the quick
  menu itself and baked it in, stacking another copy on every reopen. That path now reuses the real
  game frame saved beforehand.
- **Button waits can no longer hang the console.** Four wait-for-release loops read the raw HID
  register with no bound; a stuck pad would spin forever *with the game paused*, which presents as a
  dead 3DS. All four now go through the existing capped helper (~2s).
- **Translated text can no longer overflow the stack.** Strings loaded from SD language files (and
  author-written labels) were formatted into fixed-size stack buffers with unbounded `siprintf` —
  worst case concatenated four translations into 96 bytes. All 31 such calls are now bounded
  `sniprintf`. Also fixed `WpName()`, which took a capacity argument and discarded it.
- **Fixed enabled cheats rendering cyan instead of green, and the About screen's highlighted lines
  rendering grey-green instead of gold.** `on ? GREEN_ON : INK` looks right and compiles silently,
  but `?:` binds tighter than `,`: it expanded to `(on ? (r,g,b) : INK_r), INK_g, INK_b`, so red came
  from the wrong channel and green/blue always came from `INK`. Colours are now selected as an array
  and expanded once.

### Fixes & About (builds 165–170)
- **Fixed** the About screen's footer literally printing `{D-Pad}` instead of the D-pad icon — it
  now uses the same `{DP}` glyph token as every other footer in the plugin.
- **Checklist auto-fill now syncs to the loaded save** instead of only ever adding. Switching from a
  100% save to a lesser one (or vice-versa) now self-corrects on open — it removes auto-marks the
  current save doesn't confirm and keeps your manual checks; the result shows `+added -removed`.
- **About screen** gained the author credit (Made by samabr) and became **scrollable**, with the full
  third-party credits (walkthrough, save-data, practice-menu references) that didn't fit before.
- **Battle folder reordered** (defense/health cheats grouped first).

### Keypad restyle (builds 160–164)
- **The numeric keypad** (Cheat Search value/address, Hex Editor byte) was reskinned with **OoT3D
  keyboard art**: stone-tile digit and hex keys, a backspace key, DEC/HEX toggle, and gem OK / Cancel
  buttons. Keys and positions are pixel-matched to a reference layout; hex keys grey out in DEC mode.
- Under the hood: a scaled RGBA sprite blitter, and sprite packing switched to **round-to-nearest**
  so the 4-bit colour no longer reads slightly brighter than the source art.

### Waypoint auto-naming, favorites & quick-menu polish (builds 151–159)
- **Waypoints name themselves by area.** Saving a slot captures the scene, so slots read
  **"1. Kokiri Forest"**, **"3. Zora's Domain"**, etc. instead of "Slot N" — no typing. Dungeons
  also show the room index (**"5. Water Temple R2"**) so two saves in one dungeon stay distinct.
  Scene→name table taken from the OoT decomp scene table.
- **100% Checklist is favoritable** — star it (or any tool: Cheat Search, Hex Editor, Guides…) to
  launch it straight from the quick menu. Persisted in Favorites.txt.
- **D-Pad ←/→ now cycle the value in place** on the Waypoint Slot, Language, and MoonJump / Fast
  Move / Quick-Menu hotkey rows — in both the menu and the quick menu (shoulders still page).
- **Quick menu tidy-up**: non-toggle rows (actions, shortcuts, warps) now show a solid tinted box
  in the checkbox column so the layout lines up; real on/off toggles keep the checkmark box.
- **Fixed** the Change Theme preview not reverting the bottom screen when cancelled with **{B}**.
- **Fixed** the info box using a giant fallback font when the quick menu was the first thing opened
  after boot (the system font is now loaded up front).

### Toggle Age & new cheats (builds 136–150)
- **Toggle Age** (Misc) — become Adult or Child Link. The label and sword icon are **dynamic**,
  always showing the age you'll *switch to* (Master Sword / Kokiri Sword). It's a momentary,
  self-resetting toggle and is **favoritable**. Switches instantly in dual-age overworld scenes
  and applies on the next area load everywhere else (no more adult-reload crash).
- **Freeze Time of Day** (Time) — locks the in-game clock at the current time.
- **Skip Song Playback** (Misc) — skips the ocarina song-playing cutscene once the song is
  recognized.

### Waypoints reworked (builds 136–150)
- **9 saved-position slots** (1–9) instead of one. A **Waypoint Slot** picker (its own Z-target
  reticle icon) cycles the active slot with **{A}**, or from the quick menu; **{START}** on any
  waypoint row wipes all 9.
- **Reliable warps via the respawn mechanism** — Warp now reloads the scene and drops Link at the
  exact saved spot (right room + collision), so it works in **villages and dungeons**, not just
  open fields, and across age changes.
- **Persistent** — slots are saved to the SD card between sessions. **Save Position**, **Warp to
  Saved Position** and the **Slot** picker are all individually favoritable.

### Teleport (mapped warps)
- **New Movement → Teleport folder**: 30 mapped destinations (overworld + every dungeon) plus
  **Reload current scene**. Warps by writing the game's entrance/transition fields directly (no
  hooks) — the mechanism and entrance-index values were cross-referenced from the gamestabled and
  HylianFreddy OoT3D practice menus and verified on hardware for both child and adult Link.
- **Two-column grid** with a category **Filter** (All / Overworld / Dungeons), map-pin icons
  (Boomerang for Reload), scrolling, and abbreviated names.
- **Favorites for warps *and* folders**: star a destination for a one-press warp from the quick
  menu, or star the whole Teleport folder to open it from the quick menu. Persisted in Favorites.txt.

### Navigation & input
- **D-Pad ←/→ paging** through long folders, pickers and the Checklist; **auto-repeat** (hold a
  D-pad direction to keep scrolling) across every menu.
- **Fixed** the first row not showing as selected when a folder opens on a section header.
- N3DS **ZL/ZR** were investigated for hotkeys and shelved (they aren't in the raw HID register the
  overlay can read; only in the ir:rst/CPP block the game maps privately).

### Settings & controls
- **Rebindable MoonJump / Fast Move hotkeys** (cycle A/X/Y/L/R) + **Reset hotkeys**, grouped in
  Settings sections **GENERAL** and **IN-GAME HOTKEYS**. Quick-menu hotkey shown as a glyph.
  Defaults are now **{Y} MoonJump / {X} Fast Move**, and a feature's info card shows its live
  mapped button.
- **Config migration**: settings (theme, language, hotkeys, toggles) now survive version bumps
  instead of resetting.
- **Auto-fill Checklist on open** toggle. **Wooden info-box** skin was tried and dropped.

### Readability (auto-contrast)
- The **info box** and the Checklist status pill are now readable on every one of the 26 themes
  (light-theme text no longer vanishes on dark panels).
- The **language picker** shows any language whose SD file is missing in **red**.

### Sprites & visual identity
- **Real game sprites throughout.** HOME category icons are now game items — Movement=Hover
  Boots, Battle=Master Sword, Inventory=pouch, Weather/Time=Ocarina of Time, Quest=Boss Key,
  Misc=Mask of Truth, Tools=Megaton Hammer, Settings=Lens of Truth. Tools submenu: Cheat
  Search=Lens, RAM Dumper=Bottle, Hex Editor=Hammer, About=Zelda's Letter. Guides=Map/Compass,
  100% Checklist=Gerudo crown. ~20 checklist items got their real item icon.
- **Button glyphs** ({A}{B}{X}{Y}{L}{R}{D-Pad}) rendered inline in every control hint, the pause
  help screen, and cheat labels/info — small and large font, localized.
- **Invented icons** for the size-modifier cheats (Giant/Mini/Normal/Paper Link).
- **About logo** — real OoT3D title logo, frame matted out, "Ocarina of Time 3D" recolored white.

### Settings & controls
- **Rebindable MoonJump / Fast Move hotkeys** (cycle A/X/Y/L/R) + **Reset hotkeys**, grouped in
  Settings sections **GENERAL** and **IN-GAME HOTKEYS**. Quick-menu hotkey shown as a glyph.
- **Auto-fill Checklist on open** toggle. **Wooden info-box** skin was tried and dropped.

### 100% Checklist
- **Auto-fill on open**, **START** resets `Checklist.txt`, filter moved to the bottom screen,
  centered footers, scroll arrows.
- **Auto-fill expanded to 76 detections** (from 58): Hover Boots, Magic Meter, Trade-sequence
  completion, all **8 boss Heart Containers** (via the medallion/stone the boss drops), and **7
  story events** (Open Door of Time / Awaken as Adult / Meet Sheik / Rauru / Rescue Ruto / Cross
  Wasteland / Reach Colossus). All mapped by diffing the HTW progressive saves — zero false
  positives. Individual Gold Skulltulas and Heart Pieces stay manual (scattered scene flags,
  not isolable).

### Quick menu
- **Y** un-favorites the selected entry, **X** opens its info box, **auto-contrast** so labels
  stay readable on light themes, and labels hide any "(…)" tail to keep the panel narrow.
- **Remembers the last cursor position** when reopened.
- **Fixed** a favorited folder opened from the quick menu hijacking SELECT (HOME became
  unreachable) — menu state is now saved and restored around the shortcut.

### Naming, scope & misc polish
- Plugin renamed **OcarinaCTRComposer** (the engine stays **CTRComposer**); the file ships as
  `OcarinaCTRComposer.3gx`. **USA-only** now — EUR/JPN Title IDs dropped.
- Live theme recolor on the bottom screen, pause help re-laid-out, InfoBox titles drop "(…)",
  Tips trimmed, Movement reordered with an "Experimental" divider, Time Modifier moved last.

---

## v1.0.0 · build 90

First public release. What it does:

- **Cheats** — full set ported from the original OoT3D plugin: movement (moonjump, fast
  move, waypoint teleport), battle, inventory (swords/shields/tunics/items/bottles/gauntlets),
  time & weather, quest (medallions, stones, songs, hearts, keys, maps), and misc.
- **100% Checklist** — track completion by area, in play order (14 areas). **Auto-fill**
  reads your save and safely marks what it can (equipment, medallions, the 10 songs, spiritual
  stones, and inventory-slot items); everything else is manual. Per-item detail card with
  hint + spoiler-gated location, marquee for long text, progress bars, persistent save file.
- **Teleport waypoint** — Save Position / Warp to Saved Position (best in open areas).
- **Tools** — Cheat Search (known/unknown + undo + regions), RAM Dumper, Hex Editor.
- **Guides** — full in-game walkthrough + plugin how-to, loadable per language from SD.
- **Localization** — English, Français, Deutsch, Italiano, Español, Português (UI + guides).
- **26 color themes**, live-switchable and saved.
- **Favorites quick menu** (L+SELECT), persistent across updates.
- **SELECT** = back to game from anywhere; reopen resumes exactly where you left off.

---

## Development highlights (the road to 1.0.0)

Build numbers are exact for recent work and approximate for the early foundation.

### Waypoint teleport, input fixes & polish — builds ~082–090
- **Waypoint teleport**: Save/Warp Position using Link's live coordinates. Position offset in
  the actor was pinned empirically on hardware with a self-diagnosing "finder", not guessed.
  Custom icons (green map pin / cyan portal). Known limitation documented (multi-room scenes).
- Fixed **B swinging the sword when leaving the menu** — the plugin now waits for B/SELECT to be
  released before handing control back to the game.
- Fixed the **top screen only appearing after releasing SELECT** — the menu now paints both
  screens the instant SELECT is pressed.
- Quick menu (Favorites) now shows **every icon**, including the hand-drawn vector ones.
- **Favorites made truly persistent**: moved to their own label-keyed file so adding or removing
  cheats no longer resets them (they used to reset on every build that changed the cheat list).

### Cheat accuracy pass — builds ~078–081
- **Global RWX at init** (mirrors what CTRPluginFramework did) so cheats that write the game's
  read-only tables actually take effect.
- Researched three cheats (Can Use All Items, No Fall Damage, Voice) against two independent
  community sources; their addresses were correct but incompatible with this build's runtime,
  so they were **removed** rather than shipped broken.
- Added **Learn All Songs** (Ocarina icon). Equip toggles now show **ADDED/REMOVED** feedback.

### 100% Checklist + save-data mapping — builds ~066–077
- Built the whole **Checklist** engine (hub, per-item cards, filters, marquee, persistence).
- Authored all **14 progression areas** of item data.
- **Save-diffing breakthrough**: diffed 48 progressive community save files (HTW set) to map the
  game's quest-status bitfield exactly — unlocking reliable **auto-fill of the 10 songs, spiritual
  stones and medallions**, plus inventory-slot detection. No guessed addresses.
- Hub layout reworked: 2-line names (with "&" kept on its line), paginated grid, system font.

### Foundation — earlier builds
- **Localization**: `T()` gettext-style system, SD language files, per-language guide reader,
  translations for FR/DE/IT/ES/PT.
- **Guides**: in-game walkthrough + plugin guide, embedded English fallback.
- **Tools**: Cheat Search, RAM Dumper, Hex Editor.
- **Font foundation**: 3DS system font via APT, UTF-8 + accented-Latin glyph support.
- **Cheat port**: all codes from the original `cheats.cpp` re-expressed as direct memory writes.
- **Engine**: raw `.3gx`, self-rendered themeable UI, manual touch init, game pause via the Luma
  scheduler, toast notifications, SD persistence, quick menu — all with **no game hooks**.

---

## Credits

See [README](README.md) for full credits. Notably: original OoT3D cheats — Nanquitas;
Luma3DS / 3GX loader — LumaTeam / PabloMK7; walkthrough — z64central, packaged by LowEndC;
item sprites — The Spriters Resource (ripped by Colbydude); progressive save files used to map
the save data — HelpTheWretched (HTW).
