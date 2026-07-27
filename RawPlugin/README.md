# Ocarina CTRComposer — source

Source for the **Ocarina of Time 3D** build of **CTRComposer**, a raw `.3gx` overlay engine
for the 3DS (Luma3DS plugin loader). CTRComposer renders its own UI to the framebuffer and
runs inside the game's process, so it stays self-contained across games and Luma versions.

For the engine architecture and how to build a plugin for another game, see
[`../CTRComposer-Blank-Template.md`](../CTRComposer-Blank-Template.md).

## Features

- **~60 cheats** in folders (Movement / Battle / Inventory / Time / Quest / Misc.), both data
  cheats and code patches. Item pickers (bottle contents, inventory modifier) with real OoT3D sprites.
- **Tools:** Cheat Search, RAM Dumper, Hex Editor, About.
- **Game Guide** (embedded walkthrough reader) and Plugin Guide.
- Per-cheat info (**X**), favorites (**Y**), quick menu (**L+SELECT**), toast notifications,
  and settings persisted to the SD card.
- Renders with the 3DS system (anti-aliased) font; the menu pauses the game; double-buffered,
  no flicker.
- **Themes** (cosmetic) — the classic Zelda parchment plus 25 more color themes, switchable live.

## Controls

| Button | Action |
|---|---|
| SELECT | open the menu / return to the game |
| L+SELECT (in game) | favorites quick menu |
| D-Pad | navigate |
| A | open folder / toggle / apply |
| B | back |
| X | cheat info |
| Y | favorite (star) |

## Build

The devkitPro toolchain breaks on paths with spaces — copy `RawPlugin/` to a space-free path
and run `make` in the devkitPro msys2 shell:
`/c/devkitPro/msys2/usr/bin/bash.exe -lc 'cd <path> && make'`.

Requirements: devkitARM + libctru (pacman) and `3gxtool` 1.3 (`thepixellizeross-win/3gxtool`).

Install on the SD card: `luma/plugins/0004000000033500/CTRComposer.3gx` (USA Title ID; keep only
the one `.3gx` in that folder).

The generated headers `topbg.h`, `botbg.h`, `sprites.h`, `guide.h` and `themes.h` are produced
by the scripts under `../Tools/` from `../references/`, `../Assets/Sprites/` and the guide
sources.

## Credits

- **Nanquitas** — original OoT3D cheats plugin, cheat structure and background art ·
  https://github.com/Nanquitas
- **Fort42** — original authors of most of the cheat codes
- **The Spriters Resource** — OoT3D item icon sheet, ripped by **Colbydude** (N64 icons by GaryCXJk)
- **Walkthrough text** — z64central.com (Zelda Ocarina 3D Help); packaged for the plugin by **LowEndC** (GBAtemp)
- **CTRPluginFramework** — rendering techniques (system font, thread pause, LCD) referenced from it · https://github.com/ThePixellizerOSS
- **Luma3DS** (plugin loader) — https://github.com/LumaTeam/Luma3DS · **PabloMK7** — https://github.com/PabloMK7
- Game assets belong to **Nintendo**; fan project, non-commercial.

## License

Personal / educational use. Not for commercial redistribution.
