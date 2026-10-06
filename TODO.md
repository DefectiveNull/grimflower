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
- [X] Test SSAO and see how well it performs: in, on the world mesh only (`src/engine/ssao.cpp`, cvars `r_ssao`, `r_ssao_radius` and `r_ssao_direct`). On a GTX 1050 Ti it costs about 0.6-0.8 ms a frame at 1280x720 and 1.2-2.0 ms at 1920x1080 in the test levels, and it darkens ambient light plus half of the direct light by default (`r_ssao_direct 0.5`), since both levels have very little ambient.
= [ ] Test an alternative lighting route with lightmapping and the embree library
- [X] Test a simplified bounce lighting hack and see how it looks/performs: in, for the sun only (`src/engine/bounce.cpp`, cvars `r_bounce` and `r_bounce_scale`). One bounce, gathered from 26 directions per texel of a lightmap at 2 texels per world unit, baked on a background thread. The editor re-bakes when the sun changes and saves `maps/<level>_lightmap.png` with the level, plus a fingerprint of the world, its textures and the sun (`info.lightmapKey`); the game loads that instead of baking while it still matches, and re-bakes in the background when it doesn't, like the editor does after a world model hot reload. On a GTX 1050 Ti the dev level bakes in 1.3 s without blocking anything, loads its saved lightmap in 6 ms, and draws no slower.

## Code

- [X] **Files copied between game and editor**: `lighting`, `world`, `level`, `gamedata`, `keys` and `skybox` now live once in `src/shared`, which both build.

## Build

- [ ] **Close the game and editor before building**: `build.bat` can't replace an `.exe` that's running (`LNK1104`).
- [ ] **Harmless `vswhere.exe` message**: `vcvars64.bat` prints "'vswhere.exe' não é reconhecido..." during every build. It doesn't affect anything, but it's noise.


