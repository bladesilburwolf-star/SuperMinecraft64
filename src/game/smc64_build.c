#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sm64.h"
#include "area.h"
#include "game_init.h"
#include "mario.h"
#include "level_update.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "behavior_data.h"
#include "model_ids.h"
#include "engine/math_util.h"
#include "print.h"
#include "smc64_build.h"
#include "smc64_levelselect.h"

#define SMC64_BUILD_BLOCK_SIZE 200.0f
#define SMC64_BUILD_REACH 250.0f
#define SMC64_BUILD_MAX_BLOCKS 128
#define SMC64_INVENTORY_SLOTS 27
#define SMC64_HOTBAR_SLOTS 9
#define SMC64_STACK_SIZE 64

// Build-mode controls. SM64EX-alo only binds keyboard keys for L, R, Z and the
// C buttons by default (the D-pad is mostly unbound), so those are the primary
// buttons; the D-pad still works as an extra for controllers. Remap here.
#define SMC64_BTN_PLACE     (R_TRIG | U_JPAD)
#define SMC64_BTN_BREAK     (L_TRIG | D_JPAD)
#define SMC64_BTN_SLOT_PREV (U_CBUTTONS | L_JPAD)
#define SMC64_BTN_SLOT_NEXT (D_CBUTTONS | R_JPAD)
#define SMC64_BTN_INVENTORY Z_TRIG
#define SMC64_ALL_BUILD_BTNS (SMC64_BTN_PLACE | SMC64_BTN_BREAK | SMC64_BTN_SLOT_PREV \
                              | SMC64_BTN_SLOT_NEXT | SMC64_BTN_INVENTORY)

// Item 0 is "empty". Every other entry below is something the player can hold,
// place and break. Solid items use bhvSMC64BuildBlock (metal-box collision);
// prop items spawn with their real SM64 behavior. Only models that blockland
// already loads (common0/common1 + the global_1 model list) are used, so no extra
// actor groups are needed. Order = initial inventory order (first 9 = hotbar).
struct BuildItemDef {
    const char *name;
    const char *code;   // one letter for the slot cell (print_text has no symbols)
    ModelID32 model;
    const BehaviorScript *behavior;
    s16 yOffset;        // added to the cell center (-100 = stand on the cell floor)
    s16 startCount;
    s8 solid;
};

#define ITEM_SOLID TRUE
#define ITEM_PROP FALSE
static const struct BuildItemDef s_items[] = {
    { "EMPTY",       " ", 0,                         NULL,                  0,    0, ITEM_PROP },
    { "METAL BOX",   "M", MODEL_METAL_BOX,           bhvSMC64BuildBlock,    0,   64, ITEM_SOLID },
    { "CRATE",       "C", MODEL_BREAKABLE_BOX,       bhvSMC64BuildBlock,    0,   64, ITEM_SOLID },
    { "SMALL CRATE", "S", MODEL_BREAKABLE_BOX_SMALL, bhvSMC64BuildBlock,    0,   64, ITEM_SOLID },
    { "QUESTION",    "Q", MODEL_EXCLAMATION_BOX,     bhvSMC64BuildBlock,    0,   64, ITEM_SOLID },
    { "COIN",        "O", MODEL_YELLOW_COIN,         bhvYellowCoin,         0,   32, ITEM_PROP },
    { "RED COIN",    "R", MODEL_RED_COIN,            bhvRedCoin,            0,   16, ITEM_PROP },
    { "1 UP",        "U", MODEL_1UP,                 bhv1Up,                0,    8, ITEM_PROP },
    { "GOOMBA",      "G", MODEL_GOOMBA,              bhvGoomba,          -100,   16, ITEM_PROP },
    { "BOB OMB",     "B", MODEL_BLACK_BOBOMB,        bhvBobomb,          -100,   16, ITEM_PROP },
    { "BOMB BUDDY",  "Y", MODEL_BOBOMB_BUDDY,        bhvBobombBuddy,     -100,    8, ITEM_PROP },
    { "KOOPA SHELL", "K", MODEL_KOOPA_SHELL,         bhvKoopaShell,      -100,   16, ITEM_PROP },
    { "HEART",       "H", MODEL_HEART,               bhvRecoveryHeart,      0,   16, ITEM_PROP },
    { "FLY GUY",     "F", MODEL_FLYGUY,              bhvFlyGuy,             0,   16, ITEM_PROP },
    { "CHUCKYA",     "X", MODEL_CHUCKYA,             bhvChuckya,         -100,   16, ITEM_PROP },
    { "HOMING AMP",  "A", MODEL_AMP,                 bhvHomingAmp,          0,   16, ITEM_PROP },
    { "BUTTERFLY",   "T", MODEL_BUTTERFLY,           bhvButterfly,          0,   16, ITEM_PROP },
    { "STAR",        "Z", MODEL_STAR,                bhvStaticObject,       0,   16, ITEM_PROP },
    { "SIGN",        "N", MODEL_WOODEN_SIGNPOST,     bhvStaticObject,    -100,   16, ITEM_PROP },
};
#define SMC64_ITEM_COUNT ((s32) (sizeof(s_items) / sizeof(s_items[0])))
#define BUILD_ITEM_EMPTY 0

struct InventorySlot {
    s8 item;
    s16 count;
};

struct BuildBlock {
    struct Object *object;
    s8 item;
};

static struct BuildBlock s_blocks[SMC64_BUILD_MAX_BLOCKS];
static struct InventorySlot s_inventory[SMC64_INVENTORY_SLOTS];
static struct Area *s_area;
static struct Object *s_steveObject;
static s32 s_blockCount;
static s32 s_hotbarSlot;
static s32 s_inventoryCursor;
static s32 s_buildMode;
static s32 s_character = 1; // Preserve Steve as the default build-mode avatar.
static s32 s_inventoryOpen;
static s32 s_inventoryInitialized;
static s32 s_marioVisibilityCaptured;
static s32 s_marioWasInvisible;

s32 smc64_build_is_enabled(void) {
    return s_buildMode;
}

void smc64_build_set_enabled(s32 enabled) {
    s_buildMode = enabled != 0;
    s_inventoryOpen = FALSE;
    if (!s_buildMode) {
        if (gMarioObject != NULL && s_marioVisibilityCaptured) {
            if (s_marioWasInvisible) {
                gMarioObject->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
            } else {
                gMarioObject->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
            }
        }
        s_marioVisibilityCaptured = FALSE;
        if (s_steveObject != NULL) {
            obj_mark_for_deletion(s_steveObject);
            s_steveObject = NULL;
        }
    }
}

enum { SMC64_CHARACTER_MARIO, SMC64_CHARACTER_STEVE, SMC64_CHARACTER_RESERVED_1,
       SMC64_CHARACTER_RESERVED_2, SMC64_CHARACTER_RESERVED_3,
       SMC64_CHARACTER_RESERVED_4, SMC64_CHARACTER_RESERVED_5,
       SMC64_CHARACTER_COUNT };

s32 smc64_build_get_character(void) {
    return s_character;
}

void smc64_build_set_character(s32 character) {
    if (character < 0 || character >= SMC64_CHARACTER_COUNT) return;
    if (character == s_character) return;
    s_character = character;
    if (s_steveObject != NULL) {
        obj_mark_for_deletion(s_steveObject);
        s_steveObject = NULL;
    }
}

static void update_steve_avatar(void) {
    if (!s_buildMode || gMarioState == NULL || gMarioObject == NULL) return;

    if (!s_marioVisibilityCaptured) {
        s_marioWasInvisible = (gMarioObject->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE) != 0;
        s_marioVisibilityCaptured = TRUE;
    }
    if (s_character == SMC64_CHARACTER_MARIO) {
        if (s_marioWasInvisible) gMarioObject->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
        else gMarioObject->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
        if (s_steveObject != NULL) {
            obj_mark_for_deletion(s_steveObject);
            s_steveObject = NULL;
        }
        return;
    }

    gMarioObject->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
    if (s_character != SMC64_CHARACTER_STEVE) return; // Reserved character slot.

    if (s_steveObject == NULL) {
        s_steveObject = spawn_object_abs_with_rot(gMarioObject, 0, MODEL_STEVE,
            bhvStaticObject, gMarioState->pos[0], gMarioState->pos[1], gMarioState->pos[2],
            0, gMarioState->faceAngle[1], 0);
        if (s_steveObject == NULL) return;
        s_steveObject->header.gfx.scale[0] = 0.25f;
        s_steveObject->header.gfx.scale[1] = 0.25f;
        s_steveObject->header.gfx.scale[2] = 0.25f;
    }

    s_steveObject->oPosX = gMarioState->pos[0];
    s_steveObject->oPosY = gMarioState->pos[1];
    s_steveObject->oPosZ = gMarioState->pos[2];
    s_steveObject->oMoveAngleYaw = gMarioState->faceAngle[1];
    s_steveObject->oFaceAngleYaw = gMarioState->faceAngle[1];
}

static void initialize_inventory(void) {
    if (s_inventoryInitialized) return;
    for (s32 i = 1; i < SMC64_ITEM_COUNT && i <= SMC64_INVENTORY_SLOTS; i++) {
        s_inventory[i - 1] = (struct InventorySlot) { (s8) i, s_items[i].startCount };
    }
    s_inventoryInitialized = TRUE;
}

static struct InventorySlot *get_hotbar_slot(void) {
    return &s_inventory[s_hotbarSlot];
}

static s32 find_inventory_space(s32 item) {
    s32 empty = -1;
    for (s32 i = 0; i < SMC64_INVENTORY_SLOTS; i++) {
        if (s_inventory[i].item == item && s_inventory[i].count < SMC64_STACK_SIZE) return i;
        if (empty < 0 && s_inventory[i].count == 0) empty = i;
    }
    return empty;
}

static s32 add_item(s32 item) {
    s32 slot = find_inventory_space(item);
    if (slot < 0) return FALSE;
    if (s_inventory[slot].count == 0) s_inventory[slot].item = item;
    s_inventory[slot].count++;
    return TRUE;
}

static f32 block_center(f32 value) {
    return floorf(value / SMC64_BUILD_BLOCK_SIZE) * SMC64_BUILD_BLOCK_SIZE
         + SMC64_BUILD_BLOCK_SIZE * 0.5f;
}

static void get_target(s16 *x, s16 *y, s16 *z) {
    *x = (s16) block_center(gMarioState->pos[0] + sins(gMarioState->faceAngle[1]) * SMC64_BUILD_REACH);
    *y = (s16) block_center(gMarioState->pos[1]);
    *z = (s16) block_center(gMarioState->pos[2] + coss(gMarioState->faceAngle[1]) * SMC64_BUILD_REACH);
}

static void remove_entry(s32 index) {
    s_blockCount--;
    s_blocks[index] = s_blocks[s_blockCount];
    memset(&s_blocks[s_blockCount], 0, sizeof(s_blocks[s_blockCount]));
}

// Props can die, get collected, wander off or be unloaded by the game, and the
// object pool reuses freed slots. Drop any entry whose object is gone or is no
// longer the object we spawned, so we never touch a stale or reused pointer.
static void prune_blocks(void) {
    for (s32 i = 0; i < s_blockCount;) {
        struct Object *obj = s_blocks[i].object;
        if (obj == NULL || obj->activeFlags == ACTIVE_FLAG_DEACTIVATED
            || !obj_has_behavior(obj, s_items[(s32) s_blocks[i].item].behavior)) {
            remove_entry(i);
        } else {
            i++;
        }
    }
}

// Nearest tracked object to a point (props move, so use the live position).
static s32 find_nearest(f32 x, f32 y, f32 z, f32 radius) {
    s32 best = -1;
    f32 bestDist = radius * radius;
    for (s32 i = 0; i < s_blockCount; i++) {
        struct Object *obj = s_blocks[i].object;
        f32 dx = obj->oPosX - x;
        f32 dy = obj->oPosY - y;
        f32 dz = obj->oPosZ - z;
        f32 dist = dx * dx + dy * dy + dz * dz;
        if (dist <= bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

static void place_block(void) {
    s16 x, y, z;
    struct InventorySlot *slot = get_hotbar_slot();
    get_target(&x, &y, &z);
    if (slot->item == BUILD_ITEM_EMPTY || slot->count <= 0 || slot->item >= SMC64_ITEM_COUNT
        || find_nearest(x, y, z, 110.0f) >= 0
        || s_blockCount >= SMC64_BUILD_MAX_BLOCKS || gMarioObject == NULL) {
        return;
    }

    const struct BuildItemDef *def = &s_items[(s32) slot->item];
    // Props face Mario when placed; solid blocks keep a fixed orientation.
    s16 yaw = def->solid ? 0 : (s16) (gMarioState->faceAngle[1] + 0x8000);
    struct Object *obj = spawn_object_abs_with_rot(gMarioObject, 0, def->model,
        def->behavior, x, y + def->yOffset, z, 0, yaw, 0);
    if (!obj) return;
    s_blocks[s_blockCount].object = obj;
    s_blocks[s_blockCount].item = slot->item;
    s_blockCount++;
    slot->count--;
    if (slot->count == 0) slot->item = BUILD_ITEM_EMPTY;
}

static void destroy_block(void) {
    s16 x, y, z;
    get_target(&x, &y, &z);
    s32 index = find_nearest(x, y, z, 150.0f);
    if (index < 0) return;
    if (!add_item(s_blocks[index].item)) return;

    obj_mark_for_deletion(s_blocks[index].object);
    remove_entry(index);
}

static void move_inventory_cursor(s32 dx, s32 dy) {
    s32 row = s_inventoryCursor / 9;
    s32 col = s_inventoryCursor % 9;
    col = (col + dx + 9) % 9;
    row = (row + dy + 3) % 3;
    s_inventoryCursor = row * 9 + col;
}

static void inventory_select(void) {
    if (s_inventoryCursor < SMC64_HOTBAR_SLOTS) {
        s_hotbarSlot = s_inventoryCursor;
        return;
    }
    struct InventorySlot swap = s_inventory[s_hotbarSlot];
    s_inventory[s_hotbarSlot] = s_inventory[s_inventoryCursor];
    s_inventory[s_inventoryCursor] = swap;
}

static void clear_pressed(struct Controller *controller, u16 buttons) {
    controller->buttonPressed &= ~buttons;
    controller->buttonDown &= ~buttons;
}

// While the inventory is open Mario must not walk, jump or attack.
static void freeze_player_input(struct Controller *controller) {
    controller->rawStickX = 0;
    controller->rawStickY = 0;
    controller->stickX = 0;
    controller->stickY = 0;
    controller->stickMag = 0;
    controller->buttonPressed &= START_BUTTON;
    controller->buttonDown &= START_BUTTON;
}

// Inventory cursor direction from the stick (one step per push).
static void get_stick_step(struct Controller *c, s32 *dx, s32 *dy) {
    static s32 sHeld;
    s32 x = 0, y = 0;
    if (c->rawStickX > 60) x = 1;
    if (c->rawStickX < -60) x = -1;
    if (c->rawStickY > 60) y = -1; // stick up moves the cursor up a row
    if (c->rawStickY < -60) y = 1;
    s32 held = (x != 0 || y != 0);
    *dx = held && !sHeld ? x : 0;
    *dy = held && !sHeld ? y : 0;
    sHeld = held;
}

void smc64_build_update(void) {
    if (gMarioState == NULL || gPlayer1Controller == NULL) return;
    initialize_inventory();

    // Spawned objects are unloaded with their area. Drop their handles before
    // entering a new area so later destroy input never touches stale pointers.
    if (s_area != gCurrentArea) {
        memset(s_blocks, 0, sizeof(s_blocks));
        s_blockCount = 0;
        s_steveObject = NULL;
        s_marioVisibilityCaptured = FALSE;
        s_area = gCurrentArea;
    }

    update_steve_avatar();
    if (!s_buildMode) return;
    prune_blocks();

    if (gPlayer1Controller->buttonPressed & SMC64_BTN_INVENTORY) {
        s_inventoryOpen = !s_inventoryOpen;
        s_inventoryCursor = s_hotbarSlot;
        clear_pressed(gPlayer1Controller, SMC64_BTN_INVENTORY);
        if (s_inventoryOpen) {
            s32 dx, dy;
            get_stick_step(gPlayer1Controller, &dx, &dy); // swallow the push that was already held
        }
    }
    if (s_inventoryOpen) {
        u16 pressed = gPlayer1Controller->buttonPressed;
        s32 dx, dy;
        get_stick_step(gPlayer1Controller, &dx, &dy);
        if (pressed & (L_JPAD | L_CBUTTONS)) dx = -1;
        if (pressed & (R_JPAD | R_CBUTTONS)) dx = 1;
        if (pressed & (U_JPAD | U_CBUTTONS)) dy = -1;
        if (pressed & (D_JPAD | D_CBUTTONS)) dy = 1;
        if (dx != 0 || dy != 0) move_inventory_cursor(dx, dy);
        if (pressed & A_BUTTON) inventory_select();
        if (pressed & B_BUTTON) s_inventoryOpen = FALSE;
        freeze_player_input(gPlayer1Controller);
        return;
    }

    u16 pressed = gPlayer1Controller->buttonPressed;
    if (pressed & SMC64_BTN_SLOT_PREV) {
        s_hotbarSlot = (s_hotbarSlot + SMC64_HOTBAR_SLOTS - 1) % SMC64_HOTBAR_SLOTS;
    }
    if (pressed & SMC64_BTN_SLOT_NEXT) {
        s_hotbarSlot = (s_hotbarSlot + 1) % SMC64_HOTBAR_SLOTS;
    }
    if (pressed & SMC64_BTN_PLACE) place_block();
    if (pressed & SMC64_BTN_BREAK) destroy_block();
    clear_pressed(gPlayer1Controller, SMC64_ALL_BUILD_BTNS);
}

// print_text() only knows A-Z, 0-9, space and a few symbols: '-' is a star
// glyph, '*' is an "x" glyph, and ':' '>' '_' draw as blanks. The cursor is
// therefore the star glyph ('-').
static void print_slot(s32 x, s32 y, s32 index, s32 highlight) {
    char cell[4];
    snprintf(cell, sizeof(cell), "%c%s", highlight ? '-' : ' ', s_items[(s32) s_inventory[index].item].code);
    print_text(x, y, cell);
}

void smc64_build_render_hud(void) {
    if (!s_buildMode || smc64_levelselect_is_open()) return;
    char line[32];

    if (s_inventoryOpen) {
        struct InventorySlot *selected = &s_inventory[s_inventoryCursor];
        print_text_centered(160, 214, "INVENTORY");
        // Rows 1 and 2 are storage; row 0 (the hotbar) is drawn at the bottom.
        for (s32 i = SMC64_HOTBAR_SLOTS; i < SMC64_INVENTORY_SLOTS; i++) {
            print_slot(8 + (i % 9) * 34, 180 - ((i / 9) - 1) * 28, i, i == s_inventoryCursor);
        }
        for (s32 i = 0; i < SMC64_HOTBAR_SLOTS; i++) {
            print_slot(8 + i * 34, 112, i, i == s_inventoryCursor);
        }
        print_text_centered(160, 90, "HOTBAR");
        snprintf(line, sizeof(line), "%s *%d", s_items[(s32) selected->item].name, (int) selected->count);
        print_text_centered(160, 64, line);
        print_text_centered(160, 32, "A SELECT  B OR Z CLOSE");
        return;
    }

    // Hotbar along the bottom, name and stack of the held item above it.
    struct InventorySlot *held = get_hotbar_slot();
    for (s32 i = 0; i < SMC64_HOTBAR_SLOTS; i++) {
        print_slot(8 + i * 34, 16, i, i == s_hotbarSlot);
    }
    if (held->item != BUILD_ITEM_EMPTY) {
        snprintf(line, sizeof(line), "%s *%d", s_items[(s32) held->item].name, (int) held->count);
        print_text_centered(160, 44, line);
    }
    print_text(12, 208, "R PLACE  L BREAK");
    print_text(12, 190, "C UP DOWN SLOT");
    print_text(12, 172, "Z INVENTORY");
}
