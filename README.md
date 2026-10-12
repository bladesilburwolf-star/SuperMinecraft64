# SuperMinecraft64 — R-menu integration (v2)

## Level select = normal options submenu
No custom Mario-font overlay. Same look and navigation as Controls / Settings.

```
R → OPTIONS
  → SUPERMINECRAFT
       → BUILD MODE      [toggle]
       → LEVEL SELECT    [submenu]
            → BLOCKLAND
            → CASTLE GROUNDS
            → BOB OMB BATTLEFIELD
            → …
  → CAMERA / CONTROLS / DISPLAY / SOUND / SETTINGS
```

B goes back one submenu (standard options behavior).
A on a level warps there and closes the menu.

## Files to copy
```
include/text_options_strings.h.in
src/extras/options_menu.c
src/game/ingame_menu.c      (pause no longer lists build/level)
src/game/area.c             (optional; custom overlay path unused)
src/game/smc64_levelselect.c (optional; still used by file select if present)
```

Rebuild:
```bash
make -j$(nproc)
```
