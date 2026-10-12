/**
 * SM64Ex ↔ Luanti bridge
 *  - luanti_bridge_update()     writes mario.json
 *  - luanti_bridge_poll_edits() reads place.json / dig.json → voxels
 */
#include "luanti_bridge.h"
#include "luanti_voxels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(TARGET_N64)
void luanti_bridge_update(void) {}
void luanti_bridge_poll_edits(void) {}
#else

#include "types.h"
#include "game/mario.h"

extern struct MarioState *gMarioState;

static int s_counter = 0;
static int s_logged = 0;
static char s_last_place[512];
static char s_last_dig[512];

static float json_number(const char *buf, const char *key, float fallback) {
    char needle[48];
    snprintf(needle, sizeof needle, "\"%s\"", key);
    const char *p = strstr(buf, needle);
    if (!p || !(p = strchr(p, ':'))) return fallback;
    char *end = NULL;
    float value = strtof(p + 1, &end);
    return end == p + 1 ? fallback : value;
}

static void read_transform(const char *dir) {
    char path[768], buf[256];
    snprintf(path, sizeof path, "%s/transform.json", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return; }
    fclose(f);
    luanti_voxels_set_transform(json_number(buf, "scale", 100.0f),
        json_number(buf, "offset_x", 0.0f), json_number(buf, "offset_y", 0.0f),
        json_number(buf, "offset_z", 0.0f));
}

static void try_write_mario(const char *dir, float x, float y, float z, float yaw_deg, int act, double t) {
    char tmp[768], final_path[768];
    snprintf(tmp, sizeof tmp, "%s/mario.json.tmp", dir);
    snprintf(final_path, sizeof final_path, "%s/mario.json", dir);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\"t\":%.3f,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"yaw\":%.2f,\"act\":%d}\n",
            t, x, y, z, yaw_deg, act);
    fclose(f);
    if (rename(tmp, final_path) != 0) {
        f = fopen(final_path, "w");
        if (f) {
            fprintf(f, "{\"t\":%.3f,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"yaw\":%.2f,\"act\":%d}\n",
                    t, x, y, z, yaw_deg, act);
            fclose(f);
        }
    }
    if (!s_logged) {
        fprintf(stderr, "[luanti_bridge] mario.json → %s\n", dir);
        s_logged = 1;
    }
}

static void write_mario_all(float x, float y, float z, float yaw_deg, int act, double t) {
    const char *home = getenv("HOME");
    char path[512];
    const char *env = getenv("LUANTI_BRIDGE_DIR");
    if (env && env[0]) try_write_mario(env, x, y, z, yaw_deg, act, t);
    if (home && home[0]) {
        snprintf(path, sizeof path, "%s/minetest/worlds/SM64/sm64_luanti", home);
        try_write_mario(path, x, y, z, yaw_deg, act, t);
        snprintf(path, sizeof path, "%s/.minetest/worlds/SM64/sm64_luanti", home);
        try_write_mario(path, x, y, z, yaw_deg, act, t);
        snprintf(path, sizeof path, "%s/Super Minecraft 64/sm64_luanti", home);
        try_write_mario(path, x, y, z, yaw_deg, act, t);
    }
    try_write_mario("/tmp/sm64_luanti", x, y, z, yaw_deg, act, t);
}

void luanti_bridge_update(void) {
    if ((++s_counter % 2) != 0) return;
    if (gMarioState == NULL) return;
    float x = gMarioState->pos[0];
    float y = gMarioState->pos[1];
    float z = gMarioState->pos[2];
    float yaw_deg = (float)gMarioState->faceAngle[1] * (360.0f / 65536.0f);
    double t = 0.0;
#ifdef CLOCK_REALTIME
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        t = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    write_mario_all(x, y, z, yaw_deg, (int)gMarioState->action, t);
}

/* --- place / dig poll --- */

static int parse_event(const char *buf, int *lx, int *ly, int *lz, char *node, size_t nlen) {
    /* {"t":...,"x":1,"y":2,"z":3,"node":"default:dirt"} */
    const char *px = strstr(buf, "\"x\"");
    const char *py = strstr(buf, "\"y\"");
    const char *pz = strstr(buf, "\"z\"");
    if (!px || !py || !pz) return 0;
    *lx = (int)strtol(strchr(px, ':') + 1, NULL, 10);
    *ly = (int)strtol(strchr(py, ':') + 1, NULL, 10);
    *lz = (int)strtol(strchr(pz, ':') + 1, NULL, 10);
    node[0] = 0;
    const char *pn = strstr(buf, "\"node\"");
    if (pn) {
        const char *q1 = strchr(pn + 6, '"');
        if (q1) {
            q1++;
            const char *q2 = strchr(q1, '"');
            if (q2 && (size_t)(q2 - q1) < nlen) {
                memcpy(node, q1, (size_t)(q2 - q1));
                node[q2 - q1] = 0;
            }
        }
    }
    return 1;
}

static void try_read_event(const char *dir, const char *name, char *last, size_t last_sz,
                           void (*fn)(int, int, int, const char *)) {
    char path[768], buf[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return; }
    fclose(f);
    if (strcmp(buf, last) == 0) return; /* same event */
    strncpy(last, buf, last_sz - 1);
    last[last_sz - 1] = 0;
    int lx, ly, lz;
    char node[128];
    if (!parse_event(buf, &lx, &ly, &lz, node, sizeof node)) return;
    fn(lx, ly, lz, node);
}

static void on_place(int lx, int ly, int lz, const char *node) {
    luanti_voxels_place(lx, ly, lz, node);
}

static void on_dig(int lx, int ly, int lz, const char *node) {
    (void)node;
    luanti_voxels_dig(lx, ly, lz);
}

static void poll_dir(const char *dir) {
    read_transform(dir);
    try_read_event(dir, "place.json", s_last_place, sizeof s_last_place, on_place);
    try_read_event(dir, "dig.json", s_last_dig, sizeof s_last_dig, on_dig);
}

static int dir_has_state(const char *dir) {
    static const char *files[] = { "transform.json", "place.json", "dig.json" };
    char path[768];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        FILE *f = fopen(path, "r");
        if (f) { fclose(f); return 1; }
    }
    return 0;
}

void luanti_bridge_poll_edits(void) {
    const char *home = getenv("HOME");
    char path[512];
    const char *env = getenv("LUANTI_BRIDGE_DIR");
    if (env && env[0]) {
        poll_dir(env);
        return;
    }
    if (home && home[0]) {
        snprintf(path, sizeof path, "%s/.minetest/worlds/SM64/sm64_luanti", home);
        if (dir_has_state(path)) { poll_dir(path); return; }
        snprintf(path, sizeof path, "%s/minetest/worlds/SM64/sm64_luanti", home);
        if (dir_has_state(path)) { poll_dir(path); return; }
        snprintf(path, sizeof path, "%s/Super Minecraft 64/sm64_luanti", home);
        if (dir_has_state(path)) { poll_dir(path); return; }
    }
    poll_dir("/tmp/sm64_luanti");
}

#endif
