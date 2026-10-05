# TODO

Known gaps and loose ends, collected while building the game and the editor. Sections that are all done move to [TODO_ARCHIVE.md](TODO_ARCHIVE.md).

## Game

- [X] **Skybox**: levels store `info.skybox`, but nothing draws it yet. The game only logs that it's set.
- [X] **Map selection**: the `map <name>` console command loads `maps/<name>.json`, also from the command line as `game.exe +map <name>`. Play on the main menu still loads `maps/dev.json`, and there's no level picker yet.
- [ ] **Options aren't saved**: resolution, fullscreen, volume, mouse sensitivity and invert aim reset on every launch. They could go into `config.cfg` as cvars.
- [ ] **Sounds**: the audio device and master volume work, but there are no sounds yet.
- [X] **Multiple textures per map**: the world model is loaded by our own OBJ reader (`LoadWorldModel` in `src/shared/world.cpp`), one mesh per `usemtl` material, reading the `.mtl` and textures through the game data so it works from `gamedata.zip` too.
- [ ] **Sun shadow resolution**: one 2048×2048 shadow map covers the whole level, so big maps will get blocky shadows. Fit it to the area around the player, or use cascades.
- [ ] **Point light shadows**: only the first 4 lights cast shadows (`MAX_SHADOW_LIGHTS`). Raising it also means changing the matching number in the shader in `lighting.cpp`.
- [ ] **Cvars are numbers only**: string cvars would be needed for things like a map name.
- [X] Separate the rendering and engine specific code to a new folder `/engine` for better organization.
- [ ] Add Ladders, Doors and level changes
- [ ] Add a Crouching mechanic
- [ ] Test SSAO and see how well it performs
= [ ] Test an alternative lighting route with lightmapping and the embree library

## Code

- [X] **Files copied between game and editor**: `lighting`, `world`, `level`, `gamedata`, `keys` and `skybox` now live once in `src/shared`, which both build.

## Build

- [ ] **Close the game and editor before building**: `build.bat` can't replace an `.exe` that's running (`LNK1104`).
- [ ] **Harmless `vswhere.exe` message**: `vcvars64.bat` prints "'vswhere.exe' não é reconhecido..." during every build. It doesn't affect anything, but it's noise.


