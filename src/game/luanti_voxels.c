/**
 * Runtime voxel proxies in SM64Ex for Luanti dig/place.
 * Each place spawns a metal box (or cork box fallback) at the converted position.
 */
#include "luanti_voxels.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "types.h"
#include "object_list_processor.h"
#include "object_helpers.h"
#include "model_ids.h"
#include "behavior_data.h"
#include "sm64.h"
#include "level_update.h"
#include "memory.h"
#include "engine/math_util.h"

/* Updated from the transform.json written by the Luanti mod. */
static f32 s_scale = 100.0f;
static f32 s_off_x = 0.0f;
static f32 s_off_y = 0.0f;
static f32 s_off_z = 0.0f;

static struct LuantiVoxel s_vox[LUANTI_VOXEL_MAX];
static int s_inited = 0;

void luanti_voxels_init(void) {
    if (s_inited) return;
    memset(s_vox, 0, sizeof s_vox);
    s_inited = 1;
}

void luanti_voxels_set_transform(f32 scale, f32 off_x, f32 off_y, f32 off_z) {
    if (scale <= 0.0f) return;
    s_scale = scale;
    s_off_x = off_x;
    s_off_y = off_y;
    s_off_z = off_z;
}

void luanti_voxels_clear(void) {
    for (int i = 0; i < LUANTI_VOXEL_MAX; i++) {
        if (s_vox[i].active && s_vox[i].obj) {
            /* mark for deletion */
            s_vox[i].obj->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        }
        s_vox[i].active = 0;
        s_vox[i].obj = NULL;
    }
}

static void luanti_to_sm64(s32 lx, s32 ly, s32 lz, f32 *sx, f32 *sy, f32 *sz) {
    /* Inverse of: luanti = sm64/SCALE + OFF */
    *sx = ((f32)lx - s_off_x) * s_scale;
    *sy = ((f32)ly - s_off_y) * s_scale;
    *sz = ((f32)lz - s_off_z) * s_scale;
}

static struct Object *spawn_proxy(f32 x, f32 y, f32 z) {
    /*
     * MODEL_METAL_BOX is solid-looking; bhvPushableMetalBox or static.
     * bhvStaticObject keeps it still. Fallback: cork box.
     */
    struct Object *o = NULL;
#if defined(MODEL_METAL_BOX) && defined(bhvMetalBox)
    o = spawn_object_abs_with_rot(gCurrentObject ? gCurrentObject : gMarioObject,
                                  0, MODEL_METAL_BOX, bhvMetalBox,
                                  (s16)x, (s16)y, (s16)z, 0, 0, 0);
#elif defined(MODEL_METAL_BOX)
    o = spawn_object_abs_with_rot(gMarioObject, 0, MODEL_METAL_BOX, bhvYellowCoin,
                                  (s16)x, (s16)y, (s16)z, 0, 0, 0);
#else
    o = spawn_object_abs_with_rot(gMarioObject, 0, MODEL_WOODEN_POST, bhvWoodenPost,
                                  (s16)x, (s16)y, (s16)z, 0, 0, 0);
#endif
    /* Prefer generic spawn helper used across decomp forks */
    if (o == NULL && gMarioObject != NULL) {
        o = spawn_object(gMarioObject, MODEL_METAL_BOX, bhvYellowCoin);
        if (o) {
            o->oPosX = x;
            o->oPosY = y;
            o->oPosZ = z;
            o->oHomeX = x;
            o->oHomeY = y;
            o->oHomeZ = z;
        }
    }
    return o;
}

void luanti_voxels_place(s32 lx, s32 ly, s32 lz, const char *node) {
    luanti_voxels_init();
    /* replace existing at same cell */
    for (int i = 0; i < LUANTI_VOXEL_MAX; i++) {
        if (s_vox[i].active && s_vox[i].lx == lx && s_vox[i].ly == ly && s_vox[i].lz == lz) {
            if (s_vox[i].obj)
                s_vox[i].obj->activeFlags = ACTIVE_FLAG_DEACTIVATED;
            s_vox[i].active = 0;
            s_vox[i].obj = NULL;
        }
    }
    int slot = -1;
    for (int i = 0; i < LUANTI_VOXEL_MAX; i++) {
        if (!s_vox[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        fprintf(stderr, "[luanti_voxels] full, dropping place\n");
        return;
    }
    f32 sx, sy, sz;
    luanti_to_sm64(lx, ly, lz, &sx, &sy, &sz);
    /* Sit block on its cell center */
    sx += s_scale * 0.5f;
    sy += s_scale * 0.5f;
    sz += s_scale * 0.5f;

    struct Object *o = spawn_proxy(sx, sy, sz);
    s_vox[slot].active = 1;
    s_vox[slot].lx = lx;
    s_vox[slot].ly = ly;
    s_vox[slot].lz = lz;
    s_vox[slot].sx = sx;
    s_vox[slot].sy = sy;
    s_vox[slot].sz = sz;
    s_vox[slot].obj = o;
    fprintf(stderr, "[luanti_voxels] place (%d,%d,%d) %s -> SM64 (%.0f,%.0f,%.0f) obj=%p\n",
            lx, ly, lz, node ? node : "?", sx, sy, sz, (void *)o);
}

void luanti_voxels_dig(s32 lx, s32 ly, s32 lz) {
    luanti_voxels_init();
    for (int i = 0; i < LUANTI_VOXEL_MAX; i++) {
        if (!s_vox[i].active) continue;
        if (s_vox[i].lx == lx && s_vox[i].ly == ly && s_vox[i].lz == lz) {
            if (s_vox[i].obj)
                s_vox[i].obj->activeFlags = ACTIVE_FLAG_DEACTIVATED;
            s_vox[i].active = 0;
            s_vox[i].obj = NULL;
            fprintf(stderr, "[luanti_voxels] dig (%d,%d,%d)\n", lx, ly, lz);
            return;
        }
    }
}
