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

#define SMC64_BUILD_BLOCK_SIZE 200.0f
#define SMC64_BUILD_REACH 250.0f
#define SMC64_BUILD_MAX_BLOCKS 128
#define SMC64_INVENTORY_SLOTS 27
#define SMC64_HOTBAR_SLOTS 9
#define SMC64_STACK_SIZE 64

enum BuildItem {
    BUILD_ITEM_EMPTY,
    BUILD_ITEM_METAL,
    BUILD_ITEM_CRATE,
    BUILD_ITEM_QUESTION,
};

struct InventorySlot {
    s8 item;
    s16 count;
};

struct BuildBlock {
    struct Object *object;
    s16 x;
    s16 y;
    s16 z;
    s8 item;
};

static struct BuildBlock s_blocks[SMC64_BUILD_MAX_BLOCKS];
static const ModelID32 s_models[] = {
    0,
    MODEL_METAL_BOX,
    MODEL_BREAKABLE_BOX,
    MODEL_EXCLAMATION_BOX,
};
static const char *s_modelNames[] = { "EMPTY", "METAL", "CRATE", "QUESTION" };
static const char *s_itemCodes[] = { "-", "M", "C", "Q" };
static struct InventorySlot s_inventory[SMC64_INVENTORY_SLOTS];
static struct Area *s_area;
static struct Object *s_steveObject;
static s32 s_blockCount;
static s32 s_hotbarSlot;
static s32 s_inventoryCursor;
static s32 s_buildMode;
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

static void update_steve_avatar(void) {
    if (!s_buildMode || gMarioState == NULL || gMarioObject == NULL) return;

    if (!s_marioVisibilityCaptured) {
        s_marioWasInvisible = (gMarioObject->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE) != 0;
        s_marioVisibilityCaptured = TRUE;
    }
    gMarioObject->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;

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
    s_inventory[0] = (struct InventorySlot) { BUILD_ITEM_METAL, 64 };
    s_inventory[1] = (struct InventorySlot) { BUILD_ITEM_CRATE, 64 };
    s_inventory[2] = (struct InventorySlot) { BUILD_ITEM_QUESTION, 64 };
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

static s32 find_block(s16 x, s16 y, s16 z) {
    for (s32 i = 0; i < s_blockCount; i++) {
        if (s_blocks[i].object && s_blocks[i].x == x && s_blocks[i].y == y && s_blocks[i].z == z) {
            return i;
        }
    }
    return -1;
}

static void place_block(void) {
    s16 x, y, z;
    struct InventorySlot *slot = get_hotbar_slot();
    get_target(&x, &y, &z);
    if (slot->item == BUILD_ITEM_EMPTY || slot->count <= 0 || find_block(x, y, z) >= 0
        || s_blockCount >= SMC64_BUILD_MAX_BLOCKS || gMarioObject == NULL) {
        return;
    }

    struct Object *obj = spawn_object_abs_with_rot(gMarioObject, 0, s_models[slot->item],
        bhvSMC64BuildBlock, x, y, z, 0, 0, 0);
    if (!obj) return;
    s_blocks[s_blockCount].object = obj;
    s_blocks[s_blockCount].x = x;
    s_blocks[s_blockCount].y = y;
    s_blocks[s_blockCount].z = z;
    s_blocks[s_blockCount].item = slot->item;
    s_blockCount++;
    slot->count--;
    if (slot->count == 0) slot->item = BUILD_ITEM_EMPTY;
}

static void destroy_block(void) {
    s16 x, y, z;
    get_target(&x, &y, &z);
    s32 index = find_block(x, y, z);
    if (index < 0) return;
    if (!add_item(s_blocks[index].item)) return;

    obj_mark_for_deletion(s_blocks[index].object);
    s_blockCount--;
    s_blocks[index] = s_blocks[s_blockCount];
    memset(&s_blocks[s_blockCount], 0, sizeof(s_blocks[s_blockCount]));
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

    if (gPlayer1Controller->buttonPressed & Z_TRIG) {
        s_inventoryOpen = !s_inventoryOpen;
        s_inventoryCursor = s_hotbarSlot;
        clear_pressed(gPlayer1Controller, Z_TRIG);
    }
    if (s_inventoryOpen) {
        if (gPlayer1Controller->buttonPressed & L_JPAD) move_inventory_cursor(-1, 0);
        if (gPlayer1Controller->buttonPressed & R_JPAD) move_inventory_cursor(1, 0);
        if (gPlayer1Controller->buttonPressed & U_JPAD) move_inventory_cursor(0, -1);
        if (gPlayer1Controller->buttonPressed & D_JPAD) move_inventory_cursor(0, 1);
        clear_pressed(gPlayer1Controller, L_JPAD | R_JPAD | U_JPAD | D_JPAD);
        if (gPlayer1Controller->buttonPressed & A_BUTTON) {
            inventory_select();
            clear_pressed(gPlayer1Controller, A_BUTTON);
        }
        if (gPlayer1Controller->buttonPressed & B_BUTTON) {
            s_inventoryOpen = FALSE;
            clear_pressed(gPlayer1Controller, B_BUTTON);
        }
        return;
    }

    if (gPlayer1Controller->buttonPressed & L_JPAD) {
        s_hotbarSlot = (s_hotbarSlot + SMC64_HOTBAR_SLOTS - 1) % SMC64_HOTBAR_SLOTS;
        clear_pressed(gPlayer1Controller, L_JPAD);
    }
    if (gPlayer1Controller->buttonPressed & R_JPAD) {
        s_hotbarSlot = (s_hotbarSlot + 1) % SMC64_HOTBAR_SLOTS;
        clear_pressed(gPlayer1Controller, R_JPAD);
    }
    if (gPlayer1Controller->buttonPressed & A_BUTTON) {
        place_block();
        clear_pressed(gPlayer1Controller, A_BUTTON);
    }
    if (gPlayer1Controller->buttonPressed & B_BUTTON) {
        destroy_block();
        clear_pressed(gPlayer1Controller, B_BUTTON);
    }
}

void smc64_build_render_hud(void) {
    if (!s_buildMode) return;
    char cell[8];
    struct InventorySlot *slot = get_hotbar_slot();
    if (s_inventoryOpen) {
        print_text_centered(160, 216, "INVENTORY");
        for (s32 i = 0; i < SMC64_INVENTORY_SLOTS; i++) {
            s32 x = 8 + (i % 9) * 34;
            s32 y = 180 - (i / 9) * 30;
            snprintf(cell, sizeof(cell), "%c%s", i == s_inventoryCursor ? '>' : ' ', s_itemCodes[s_inventory[i].item]);
            print_text(x, y, cell);
        }
        struct InventorySlot *selected = &s_inventory[s_inventoryCursor];
        print_text(12, 76, "SELECTED ITEM");
        print_text(180, 76, s_modelNames[selected->item]);
        print_text_fmt_int(12, 56, "STACK COUNT %d", selected->count);
        print_text_centered(160, 32, "A SELECT  B OR Z CLOSE");
        return;
    }

    print_text(12, 208, "BUILD MODE");
    print_text(12, 188, "A PLACE  B BREAK");
    for (s32 i = 0; i < SMC64_HOTBAR_SLOTS; i++) {
        s32 x = 12 + i * 34;
        struct InventorySlot *hotbar = &s_inventory[i];
        snprintf(cell, sizeof(cell), "%c%s", i == s_hotbarSlot ? '>' : ' ',
            s_itemCodes[hotbar->item]);
        print_text(x, 20, cell);
    }
    print_text(12, 48, "D-PAD LEFT/RIGHT SLOT");
    print_text(12, 64, "Z INVENTORY");
    print_text(12, 88, "SELECTED ITEM");
    print_text(180, 88, s_modelNames[slot->item]);
    print_text_fmt_int(12, 108, "STACK COUNT %d", slot->count);
    print_text_fmt_int(12, 128, "BLOCKS BUILT %d", s_blockCount);
}
