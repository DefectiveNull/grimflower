# TODO

Known gaps and loose ends, collected while building the game and the editor.

## Game

- [X] **Skybox**: levels store `info.skybox`, but nothing draws it yet. The game only logs that it's set.
- [ ] **Map selection**: Play always loads `maps/dev.json`. A `map <name>` console command and/or a level picker would fix that.
- [ ] **Options aren't saved**: resolution, fullscreen, volume, mouse sensitivity and invert aim reset on every launch. They could go into `config.cfg` as cvars.
- [ ] **Sounds**: the audio device and master volume work, but there are no sounds yet.
- [ ] **Multiple textures per map**: raylib reads `.mtl` files from disk and skips `gamedata.zip`, so per-face materials (`usemtl`) aren't assigned. Single-texture maps work. The fix is assigning materials ourselves from the OBJ's `usemtl` lines.
- [ ] **Sun shadow resolution**: one 2048×2048 shadow map covers the whole level, so big maps will get blocky shadows. Fit it to the area around the player, or use cascades.
- [ ] **Point light shadows**: only the first 4 lights cast shadows (`MAX_SHADOW_LIGHTS`). Raising it also means changing the matching number in the shader in `lighting.cpp`.
- [ ] **Cvars are numbers only**: string cvars would be needed for things like a map name.
- [ ] Add Ladders, Doors and level changes
- [ ] Add a Crouching mechanic
- [ ] Test SSAO and see how well it performs
= [ ] Test an alternative lighting route with lightmapping and the embree library

## Editor

- [X] **Direction gizmo**: `direction` parameters (like the sun's) can only be typed as text. A drag handle in the viewport would help.
- [ ] **Move gizmo and snapping**: entities drag on a plane (Alt for vertical), with no axis handles or grid snapping.
- [ ] **Preferences dialog**: it's a placeholder. Camera speed and sensitivity can only be changed in `editor.cfg`.
- [ ] **`editor.cfg` is overwritten on exit**: hand edits made while the editor is open are lost.
- [ ] **Reload the entity list**: `entitylist.txt` is only read at startup, so the editor needs a restart after rebuilding the game.
- [ ] **Light preview duplicates game code**: `ApplyLevelLights` in `src/editor/levellights.cpp` copies how the game spawns `sun` and `light`. New light types need adding there too.
- [ ] **Undo history**: resets when opening or creating a level, and keeps the last 200 steps.

## Code

- [X] **Files copied between game and editor**: `lighting`, `world`, `level`, `gamedata`, `keys` and `skybox` now live once in `src/shared`, which both build.

## Build

- [ ] **Close the game and editor before building**: `build.bat` can't replace an `.exe` that's running (`LNK1104`).
- [ ] **Harmless `vswhere.exe` message**: `vcvars64.bat` prints "'vswhere.exe' não é reconhecido..." during every build. It doesn't affect anything, but it's noise.


