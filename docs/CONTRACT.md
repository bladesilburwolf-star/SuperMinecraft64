# SMC64 native builder and legacy bridge contract

## Native SM64 build mode

The native builder runs entirely inside the SM64 PC port and does not read or write Luanti state. In normal play:

- The pause menu has a `BUILD MODE ON/OFF` option. In castle-style pause menus, scroll down past the course entries to reach it. Selecting it hides Mario and shows the local cuboid Steve-style avatar; the avatar follows Mario's position and facing but holds a static pose.
- A places the selected block one grid cell in front of Mario; B destroys a block in that same target cell.
- D-pad left/right selects among 9 hotbar slots. Z opens the 27-slot inventory; the D-pad moves the cursor, A selects/swaps a slot, and B or Z closes it. The first three hotbar slots start with 64 metal boxes, breakable crates, and exclamation blocks. Destroyed blocks return to inventory when space is available.
- The HUD shows the hotbar, selected item, stack count, and inventory grid. The limit is 128 blocks in the current area.
- Built blocks use native SM64 object rendering and metal-box collision. They are removed when the area changes and are not saved between runs.
- The old Luanti bridge sources remain in the tree, but the PC game loop no longer calls them. Native building has no tunnel or Minetest dependency.
- The main file-select screen displays `Z: BLOCKLAND`; pressing Z launches the existing `LEVEL_BLOCKLAND` directly. This shortcut does not consume or overwrite a save slot.

## First slice: place and dig events

- Luanti owns the editable voxel world. Its `sm64_bridge` mod emits one `place.json` or `dig.json` event after a player places or digs a node.
- The SM64 PC port owns Mario and the rendered scene. Its frame update polls events and creates or removes a proxy object for each cell.
- `transform.json` is written by the Luanti mod and is the shared coordinate contract: scale is SM64 units per Luanti node; offsets are the Luanti coordinates corresponding to SM64 origin. All axes retain their names and positive direction (X→X, Y→Y, Z→Z).
- A Luanti node at integer `(x,y,z)` maps to the center `(x+0.5,y+0.5,z+0.5)` in node space, then to SM64 units using `(node - offset) * scale`.
- `/sm64follow` places the Luanti player at Mario's mapped position without snapping to nearby terrain. Terrain snapping changes the coordinates where blocks are built and breaks the cross-game alignment.
- State crosses through JSON files in the world’s `sm64_luanti` directory. `LUANTI_BRIDGE_DIR` can select a shared directory explicitly. Mario state is sampled at 30 Hz; edit files are polled during the PC game frame update.
- Proxy capacity is currently 256 active cells. A newer event replaces the state for the same cell. The current event files are latest-value snapshots, so bursts faster than the SM64 poll can overwrite intermediate edits.
- This slice transfers block appearance as SM64 proxy objects; it does not transfer Luanti textures, inventory, crafting, fluids, redstone-like logic, or collision as a full voxel world.

## Lifecycle and failure behavior

- If either game is closed, its process stops reading or writing; no network service is required.
- Missing or malformed files are ignored. The next write or edit can resume the bridge.
- Each game uses the same local filesystem path. Separate machines need a later transport implementation with the same JSON contract.
- Runtime alignment changes through `/sm64offset` are written to `transform.json` and applied by SM64 while polling.
