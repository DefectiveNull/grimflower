# TODO

Known gaps and loose ends, collected while building the game and the editor. Sections that are all done move to [TODO_ARCHIVE.md](TODO_ARCHIVE.md).

## Game

- [X] **Skybox**: levels store `info.skybox`, but nothing draws it yet. The game only logs that it's set.
- [X] **Map selection**: the `map <name>` console command loads `maps/<name>.json`, also from the command line as `game.exe +map <name>`. Play on the main menu still loads `maps/dev.json`, and there's no level picker yet.
- [X] **Options aren't saved**: the options are now cvars (`vid_width`, `vid_height`, `vid_fullscreen`, `volume`, `sensitivity`, `m_invert`), saved to `config.cfg` whenever they change and loaded at startup. The options menu also has an Input tab for rebinding keys, saved the same way.
- [ ] **Sounds**: the audio system is in (`src/engine/audio.cpp`), with a world and a music channel, each with its own volume in the options, and the `play` and `music` console commands. There are no sound files yet, and nothing in the game plays any: footsteps, jumping, landing, and music per level are next.
- [X] **Multiple textures per map**: the world model is loaded by our own OBJ reader (`LoadWorldModel` in `src/shared/world.cpp`), one mesh per `usemtl` material, reading the `.mtl` and textures through the game data so it works from `gamedata.zip` too.
- [ ] **Sun shadow resolution**: one 2048×2048 shadow map covers the whole level, so big maps will get blocky shadows. Fit it to the area around the player, or use cascades.
- [ ] **Point light shadows**: only the first 4 lights cast shadows (`MAX_SHADOW_LIGHTS`). Raising it also means changing the matching number in the shader in `lighting.cpp`.
- [ ] **Cvars are numbers only**: string cvars would be needed for things like a map name.
- [X] Separate the rendering and engine specific code to a new folder `/engine` for better organization.
- [X] Add Ladders, Doors and level change triggers: `func_water`, `func_ladder` and `trigger_changelevel` are volumes, boxes that do what their entity type says while the player is inside them (`src/game/volumes.cpp`). `func_door` is a solid textured box that slides open when walked into. Their bounds are typed in the editor, and they move like other entities.
- [X] Add a Crouching mechanic: hold Ctrl (`+duck`) for a half-height hull at a third of the speed, standing back up once there's room.
- [X] Add a swimming mechanic: waist deep in a water volume, the player swims where they look, jump and crouch go up and down, and jump at the surface climbs out.
- [ ] Test SSAO and see how well it performs
= [ ] Test an alternative lighting route with lightmapping and the embree library
- [ ] Test a simplified bounce lighting hack and see how it looks/performs

## Code

- [X] **Files copied between game and editor**: `lighting`, `world`, `level`, `gamedata`, `keys` and `skybox` now live once in `src/shared`, which both build.

## Build

- [ ] **Close the game and editor before building**: `build.bat` can't replace an `.exe` that's running (`LNK1104`).
- [ ] **Harmless `vswhere.exe` message**: `vcvars64.bat` prints "'vswhere.exe' não é reconhecido..." during every build. It doesn't affect anything, but it's noise.


