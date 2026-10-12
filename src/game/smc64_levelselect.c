#include <stdio.h>

#include "sm64.h"
#include "course_table.h"
#include "level_table.h"
#include "area.h"
#include "audio/external.h"
#include "game_init.h"
#include "level_update.h"
#include "print.h"
#include "smc64_levelselect.h"

// Both are non-static in their own files but are not declared in a header.
extern void initiate_warp(s16 destLevel, s16 destArea, s16 destWarpNode, s32 warpFlags);
extern void shade_screen(void);

#define ROWS_VISIBLE 9
#define ROW_STEP 18
#define LIST_TOP_Y 186

// print_text() only draws A-Z, 0-9 and space, so names are plain capitals.
struct LevelEntry {
    s16 level;
    const char *name;
};

static const struct LevelEntry s_levels[] = {
    { LEVEL_BLOCKLAND,        "BLOCKLAND" },
    { LEVEL_CASTLE_GROUNDS,   "CASTLE GROUNDS" },
    { LEVEL_CASTLE,           "CASTLE INSIDE" },
    { LEVEL_CASTLE_COURTYARD, "CASTLE COURTYARD" },
    { LEVEL_BOB,              "BOB OMB BATTLEFIELD" },
    { LEVEL_WF,               "WHOMPS FORTRESS" },
    { LEVEL_JRB,              "JOLLY ROGER BAY" },
    { LEVEL_CCM,              "COOL COOL MOUNTAIN" },
    { LEVEL_BBH,              "BIG BOOS HAUNT" },
    { LEVEL_HMC,              "HAZY MAZE CAVE" },
    { LEVEL_LLL,              "LETHAL LAVA LAND" },
    { LEVEL_SSL,              "SHIFTING SAND LAND" },
    { LEVEL_DDD,              "DIRE DIRE DOCKS" },
    { LEVEL_SL,               "SNOWMANS LAND" },
    { LEVEL_WDW,              "WET DRY WORLD" },
    { LEVEL_TTM,              "TALL TALL MOUNTAIN" },
    { LEVEL_THI,              "TINY HUGE ISLAND" },
    { LEVEL_TTC,              "TICK TOCK CLOCK" },
    { LEVEL_RR,               "RAINBOW RIDE" },
    { LEVEL_PSS,              "PEACHS SECRET SLIDE" },
    { LEVEL_SA,               "SECRET AQUARIUM" },
    { LEVEL_COTMC,            "METAL CAP CAVERN" },
    { LEVEL_VCUTM,            "VANISH CAP UNDER" },
    { LEVEL_WMOTR,            "WING MARIO RAINBOW" },
    { LEVEL_TOTWC,            "TOWER OF WING CAP" },
    { LEVEL_BITDW,            "BOWSER DARK WORLD" },
    { LEVEL_BITFS,            "BOWSER FIRE SEA" },
    { LEVEL_BITS,             "BOWSER IN THE SKY" },
    { LEVEL_BOWSER_1,         "BOWSER 1 FIGHT" },
    { LEVEL_BOWSER_2,         "BOWSER 2 FIGHT" },
    { LEVEL_BOWSER_3,         "BOWSER 3 FIGHT" },
};
#define LEVEL_COUNT ((s32) (sizeof(s_levels) / sizeof(s_levels[0])))

static s32 s_open;
static s32 s_ignoreInput;
static s32 s_cursor;
static s32 s_dir;
static s32 s_holdFrames;
static s32 s_startLevel = LEVEL_BLOCKLAND;

enum { RESULT_NONE, RESULT_CONFIRM, RESULT_CANCEL };

void smc64_levelselect_open(void) {
    s_open = TRUE;
    s_dir = 0;
    s_holdFrames = 0;
}

s32 smc64_levelselect_is_open(void) {
    return s_open;
}

s32 lvl_smc64_get_start_level(UNUSED s16 arg, UNUSED s32 unused) {
    return s_startLevel;
}

static void freeze_input(struct Controller *c) {
    c->rawStickX = 0;
    c->rawStickY = 0;
    c->stickX = 0;
    c->stickY = 0;
    c->stickMag = 0;
    c->buttonPressed = 0;
    c->buttonDown = 0;
}

static void move_cursor(s32 delta, s32 wrap) {
    s32 next = s_cursor + delta;
    if (wrap) {
        next = (next + LEVEL_COUNT) % LEVEL_COUNT;
    } else {
        if (next < 0) next = 0;
        if (next >= LEVEL_COUNT) next = LEVEL_COUNT - 1;
    }
    if (next != s_cursor) {
        s_cursor = next;
        play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
    }
}

// Stick (with hold-to-repeat), D-pad, C-Up/Down scroll by one; L/R page.
static s32 process_input(void) {
    struct Controller *c = gPlayer1Controller;
    u16 pressed = c->buttonPressed;
    s32 dir = 0;

    if (c->rawStickY > 60 || (pressed & (U_JPAD | U_CBUTTONS))) dir = -1;
    if (c->rawStickY < -60 || (pressed & (D_JPAD | D_CBUTTONS))) dir = 1;

    if (dir == 0) {
        s_holdFrames = 0;
    } else if (dir != s_dir) {
        s_holdFrames = 0;
        move_cursor(dir, TRUE);
    } else {
        s_holdFrames++;
        if (s_holdFrames >= 18 && (s_holdFrames - 18) % 6 == 0) move_cursor(dir, TRUE);
    }
    s_dir = dir;

    if (pressed & L_TRIG) move_cursor(-ROWS_VISIBLE, FALSE);
    if (pressed & R_TRIG) move_cursor(ROWS_VISIBLE, FALSE);

    if (s_ignoreInput > 0) {
        s_ignoreInput--;
        freeze_input(c);
        return RESULT_NONE;
    }

    s32 result = RESULT_NONE;
    if (pressed & (A_BUTTON | START_BUTTON)) result = RESULT_CONFIRM;
    if (pressed & B_BUTTON) result = RESULT_CANCEL;

    freeze_input(c);
    return result;
}

static void warp_to_level(s16 level) {
    s16 node = WARP_NODE_0A;
    if (level == LEVEL_CASTLE) node = WARP_NODE_00;
    gSavedCourseNum = COURSE_NONE;
    initiate_warp(level, 1, node, WARP_FLAGS_NONE);
    fade_into_special_warp(0, 0);
}

void smc64_levelselect_update(void) {
    if (!s_open || gPlayer1Controller == NULL) return;

    s32 result = process_input();
    if (result == RESULT_CANCEL) {
        s_open = FALSE;
    } else if (result == RESULT_CONFIRM) {
        s_open = FALSE;
        play_sound(SOUND_MENU_STAR_SOUND, gGlobalSoundSource);
        warp_to_level(s_levels[s_cursor].level);
    }
}

s32 smc64_levelselect_update_menu(void) {
    if (!s_open || gPlayer1Controller == NULL) return 0;

    s32 result = process_input();
    if (result == RESULT_CANCEL) {
        s_open = FALSE;
    } else if (result == RESULT_CONFIRM) {
        s_open = FALSE;
        s_startLevel = s_levels[s_cursor].level;
        play_sound(SOUND_MENU_STAR_SOUND, gGlobalSoundSource);
        return s_startLevel;
    }
    return 0;
}

void smc64_levelselect_render(void) {
    if (!s_open) return;

    shade_screen();

    // Keep the cursor inside a scrolling window of ROWS_VISIBLE rows.
    s32 first = s_cursor - ROWS_VISIBLE / 2;
    if (first > LEVEL_COUNT - ROWS_VISIBLE) first = LEVEL_COUNT - ROWS_VISIBLE;
    if (first < 0) first = 0;

    char line[40];
    print_text_centered(160, 216, "LEVEL SELECT");
    for (s32 i = 0; i < ROWS_VISIBLE && first + i < LEVEL_COUNT; i++) {
        s32 index = first + i;
        // '-' is the star glyph in print_text(), used as the cursor.
        snprintf(line, sizeof(line), "%c %s", index == s_cursor ? '-' : ' ', s_levels[index].name);
        print_text(28, LIST_TOP_Y - i * ROW_STEP, line);
    }

    snprintf(line, sizeof(line), "%d OF %d", (int) (s_cursor + 1), (int) LEVEL_COUNT);
    print_text_centered(160, 30, line);
    print_text_centered(160, 10, "A GO  B BACK  L R PAGE");
}
