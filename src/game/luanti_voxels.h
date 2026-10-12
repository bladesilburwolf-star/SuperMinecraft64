#ifndef LUANTI_VOXELS_H
#define LUANTI_VOXELS_H

#include "types.h"

#define LUANTI_VOXEL_MAX 256

struct LuantiVoxel {
    s16 active;
    s32 lx, ly, lz;       /* Luanti node coords */
    f32 sx, sy, sz;       /* SM64 units */
    struct Object *obj;   /* proxy object in level, or NULL */
};

void luanti_voxels_init(void);
void luanti_voxels_set_transform(f32 scale, f32 off_x, f32 off_y, f32 off_z);
void luanti_voxels_place(s32 lx, s32 ly, s32 lz, const char *node);
void luanti_voxels_dig(s32 lx, s32 ly, s32 lz);
void luanti_voxels_clear(void);

#endif
