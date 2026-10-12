# MODLOG

## 2026-10-11 — Native SMC64 build mode

- Replaced the tunnel-dependent block-building goal with a native SM64 PC feature.
- Controls: pause menu's `BUILD MODE ON/OFF` row toggles the feature; D-pad left/right changes the hotbar slot; Z opens inventory; the D-pad moves through its 27 slots; A selects/swaps and B or Z closes it; A places and B destroys.
- Added a 9-slot hotbar with 27 inventory slots and stack counts. The initial hotbar has 64 metal boxes, crates, and question blocks in its first three slots; destroyed blocks return to inventory.
- Added a hand-authored, cuboid Steve-style avatar model with skin, hair, shirt, pants, and facial colors. Build mode hides Mario and shows the avatar following Mario's position and facing. It is a static pose and uses no generated/paid asset service.
- Added the build-mode option to both pause menu layouts and enabled D-pad navigation in pause menus.
- Built blocks use native SM64 metal-box collision and rendering, are capped at 128 per area, and clear on area changes. They currently do not persist after leaving an area or restarting.
- The main file-select screen shows `Z: BLOCKLAND`; pressing Z launches the existing `LEVEL_BLOCKLAND` level with a valid save slot.
- HUD displays the hotbar, selected item, counts, and inventory grid. No Minetest files, network link, or monthly asset generation is needed.
- Disabled the old Luanti bridge calls from the live game loop; its source remains available but is no longer required or polled by the native game.
- Build: `make -j4` succeeded for the Linux PC target at `build/us_pc/sm64.us.f3dex2e`. In-game verification was not run in this turn.

## 2026-10-11 — Luanti block edits in SM64 PC

- Route: local passthrough/state exchange over the existing JSON file tunnel. Luanti remains the world authority; the SM64 PC port renders proxy cells.
- Existing bridge: `.minetest/mods/sm64_bridge/init.lua` writes `place.json` and `dig.json`; `smc64/src/game/luanti_bridge.c` already parsed them and called `luanti_voxels_*`, but the frame loop did not call `luanti_bridge_poll_edits()`.
- Changes: poll edit events each SM64 frame; share scale and origin offsets through `transform.json` so the Luanti marker/follow transform and SM64 voxel transform agree.
- Follow-up diagnosis: runtime logs showed Mario mapped near Luanti `(-52.7, 63.1, -5.2)`, while nearby ground snapping moved the player to roughly Y=15. The SM64 build was newer than the bridge C sources and the place event was written, so the objects were being sent to a different vertical location. `/sm64follow` now uses the exact mapped position.
- First slice: one placed/destroyed node appears/disappears as a proxy object. Full mechanic transfer is outside this slice; see `docs/CONTRACT.md` for current limits and lifecycle.
- Verification: not run in game during this change. The existing executable predates these source edits and must be rebuilt before runtime verification.
- Next: rebuild the Linux PC port, launch Luanti world `SM64` and the rebuilt port, place/dig a node, and inspect bridge/game logs. If proxy geometry is not a cube or collisions are unreliable, replace the current object behavior with a dedicated voxel proxy model/behavior.
