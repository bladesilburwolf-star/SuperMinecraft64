# Blockland: a Minecraft-style world as a Super Mario 64 level (sm64ex-alo)

`blockland.py` generates a normal SM64 level (`levels/blockland/`) containing a block world: grass, dirt, stone,
sand and gravel terrain, snow caps, trees, a sea with a water box (Mario can swim), a world-edge wall, and a ring of
coins at the start. It goes through the engine's normal level format, so nothing in the game engine had to change.

## Quick start (Debian / Ubuntu)

```sh
sudo apt install -y build-essential git python3 pkgconf libsdl2-dev libglew-dev
git clone https://github.com/AloUltraExt/sm64ex-alo
cd sm64ex-alo
git config core.fileMode false && chmod -R 775 .

cp /path/to/your/baserom.us.z64 .          # your own US ROM, named exactly baserom.us.z64
unzip -o /path/to/blockland-for-sm64ex-alo.zip   # puts tools/blockland/ into this folder

python3 tools/blockland/blockland.py              # (run inside the game folder; or add --sm64 PATH) writes levels/blockland and registers LEVEL_BLOCKLAND
make -j4                                          # builds the native PC version (extracts assets from your ROM first)

ls build/us_pc/sm64.*                             # the executable
./build/us_pc/sm64.us.f3dex2e --level 1 --skip-intro
```

* Blockland takes over the unused level slot `LEVEL_UNKNOWN_1`, which is level id **1**
  (`--listlevels` prints all ids; `--levelselect` opens a level menu).
* Already extracted the assets in this folder? Add `NOEXTRACT=1` to `make`.
* Rebuilds after re-generating are quick: only the level files change.

## If `make` says `level_blockland_entry undeclared`

An earlier `make` left stale generated files behind (make does not notice that `levels/level_defines.h` changed).
Re-running the generator removes them automatically; or do it by hand and build again:

```sh
rm -f build/us_pc/include/level_headers.h build/us_pc/level_rules.mk
make -j4
```

## Options

| Option | Meaning |
|---|---|
| `--seed N` | different world (default 7) |
| `--size N` | world width in blocks, 16-160 (default 96). Bigger = more triangles |
| `--mc-jar PATH` | use the block textures from **your own** Minecraft jar, e.g. `~/.minecraft/versions/1.21.1/1.21.1.jar`. Grass, leaves and water are tinted like a plains biome. Without it, simple original stand-in textures are used |
| `--start-here` | optional: a normal new game starts in Blockland (patches `include/tweaks.h` and `levels/menu/script.c`; set `START_LEVEL` back to `LEVEL_CASTLE_GROUNDS` to undo). Not needed if you use `--level 1` |
| `--no-register` | do not touch `levels/level_defines.h` |

Run the generator again after changing options, then `make`.

## Preview without the game

```sh
sudo apt install -y python3-numpy python3-pil
python3 tools/blockland/preview.py --sm64 . --out preview
```
writes `preview_ground.png` and `preview_overview.png` by rasterising the generated display lists with the same
back-face culling the N64 uses. `sample_*.png` in this folder were made that way.

## What has been verified

* The generated C compiled with the real sm64ex-alo build system (`script.c`, `geo.c`, `leveldata.c`, and the
  patched `levels/menu/script.c`), and the block PNGs converted with its own `n64graphics`.
* The display lists and collision were read back with independent parsers: all triangles resolved, every texture
  found, UVs in range, collision classified into floors / ceilings / vertical walls by the engine's own rule.
* A port of Mario's movement ran 12,000 random frames on the collision: stays on the terrain, never escapes the world.
* Winding order was checked against real decomp levels (93-96% of their floors are wound the same way).

**Not verified: running the game itself.** I could not build the full game (no ROM here), so the first launch is the
first real test. If something looks wrong, a screenshot or the crash/terminal text helps most.

## Known limits (first version)

* Static world: you cannot break or place blocks yet.
* Leaves are solid (Mario walks on tree tops). Water is flat (one water box at sea level); the visible water uses a
  Minecraft-ish translucent surface, not SM64's animated water.
* Mario is still Mario: no Steve model, no Minecraft mobs yet.
* About 25,000 triangles at the default size: fine for a PC, not meant for a real N64.

## Next steps

1. Break / place blocks at runtime (dynamic chunk meshes and collision).
2. Steve model, Minecraft mobs as enemies.
3. Biomes, caves, ores, villages.
