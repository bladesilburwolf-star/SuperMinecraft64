#ifndef SMC64_LEVELSELECT_H
#define SMC64_LEVELSELECT_H

#include "types.h"

// Open the overlay (pause menu row / file-select Z shortcut).
void smc64_levelselect_open(void);
s32 smc64_levelselect_is_open(void);

// In a level: call once per frame from play_mode_normal. Confirming a level warps there.
void smc64_levelselect_update(void);

// On the file-select screen: returns the chosen level number once the player
// confirms, otherwise 0. Also freezes the file-select input while open.
s32 smc64_levelselect_update_menu(void);

// Dims the screen and queues the list text; call from render_game.
void smc64_levelselect_render(void);

// Level script hook (CALL): returns the level chosen in the file-select overlay.
s32 lvl_smc64_get_start_level(s16 arg, s32 unused);

#endif
