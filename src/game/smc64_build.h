#ifndef SMC64_BUILD_H
#define SMC64_BUILD_H

void smc64_build_update(void);
void smc64_build_render_hud(void);
s32 smc64_build_is_enabled(void);
void smc64_build_set_enabled(s32 enabled);
s32 smc64_build_get_character(void);
void smc64_build_set_character(s32 character);

#endif
