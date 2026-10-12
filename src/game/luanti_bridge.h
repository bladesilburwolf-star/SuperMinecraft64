#ifndef LUANTI_BRIDGE_H
#define LUANTI_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/** Write Mario state to mario.json (call each frame). */
void luanti_bridge_update(void);

/** Poll place.json / dig.json and spawn/despawn proxy blocks (call each frame). */
void luanti_bridge_poll_edits(void);

#ifdef __cplusplus
}
#endif

#endif
