# TODO

Known gaps and loose ends, collected while building the game and the editor. Sections that are all done move to [TODO_ARCHIVE.md](TODO_ARCHIVE.md).


# Geo Editing

- [X] Add a marker for where in the world cut tool is aimed at: the cut tool's dot follows the mouse over meshes, on the grid point a click would cut through.
- [ ] Add proper shaded unlit view
- [ ] Add proper "copy unique" functionality.
- [X] Add texture manpulation and autostitch tools: Texturing mode (Tab past Modelling mode) puts textures on faces with right click, autostitches them across a surface with Shift+right click, and justifies (Shift+arrows) and fits (Shift+E) them.
- [ ] Add hotspot textures and the editor.
- [ ] Fix the shortcuts problem
- [X] Add the current grid size to the toolbar
## Game

- [ ] **Sounds**: the audio system is in (`src/engine/audio.cpp`), with a world and a music channel, each with its own volume in the options, and the `play` and `music` console commands. There are no sound files yet, and nothing in the game plays any: footsteps, jumping, landing, and music per level are next.
- [ ] **Cvars are numbers only**: string cvars would be needed for things like a map name.
- [X] Move the entity list into the details panel: with nothing selected, the details panel lists the level's entities, then a Create Entity section with a button per entity type (described when hovered). The Create Entity dialog is gone; Edit > Create Entity and `create_entity` deselect so the list shows.
- [X] add a "view" submenu that lets you hide and shot volumes among other things: View menu with Grid, Entity Labels, Point Entities, Volumes and Collision Brushes, each On/Off and saved in editor.cfg. Hidden entities can't be clicked in the view, but can still be picked from the details panel.
- [X] Add a material browser tab to the details panel: the Materials tab lists every texture in textures/ by subfolder, and gives each a bump map, specular map and gloss map (pre-PBR, like Source), saved to `.materials` and used by the game.
- [X] add a generic static prop entity as well as a generic soundscape trigger volume: `prop_model` shows any .obj (with its .mtl textures and .materials), casts shadows, and with `static` on is solid (its bounding box). `trigger_soundscape` loops a sound inside its box, fading out over `fade` units towards the sides, panned towards its origin.
- [X] bounce needs to take in  bump maps: the bake stores four layers (head on, plus Half-Life 2's three tilted directions) and bump mapped materials blend them by each texel's bump, so bumps catch the bounce light too.
- [X] add cubemap entity: `env_cubemap` captures the level from its spot once when the level loads (lights where they start) and again when a bounce bake finishes. Materials with a Reflection mirror the nearest one, masked by their specular map.
- [X] add support for materials being directly callable by func entities: `func_door`'s texture brings its material from .materials (bump, specular, gloss and reflection).

## Issues and known bugs

- [X] There's an issue with the game's options UI where now that there's a separate Input tab the organization looks weird: the General tab is gone, and its sections are tabs of their own, so the options menu has Video, Audio, Game and Input tabs.
- [X] There's an issue in the editor where it doesn't have ways of sizing our func ents still even after there was a commit to fix that: a selected volume now has a handle in the middle of each face, and dragging one moves that face along its axis, snapped to the grid.
- [ ] There's a bug in the editor where the water volume shows all wrong. and it shouldn't show the texture in the editor it should only show the volume bounds.
- [X] There's a visual big in the game and editor that creates weird lighting artifacts on faces
- [ ] There's a potential crash if the world model updates, don't understand it.
- [X] There's an issue with the player movement can't get out of the water if there's a small gap between where the water volume ends and a hard edge begins: the hop out of water fell just short of a ledge above the surface. There's now a water jump like Quake's: swimming forward into a wall at the surface, with a ledge up to 1.25 above the waist, jumps just high enough to land on it.

## Code

- [X] **Files copied between game and editor**: `lighting`, `world`, `level`, `gamedata`, `keys` and `skybox` now live once in `src/shared`, which both build.

## Build

- [ ] **Close the game and editor before building**: `build.bat` can't replace an `.exe` that's running (`LNK1104`).
- [ ] **Harmless `vswhere.exe` message**: `vcvars64.bat` prints "'vswhere.exe' não é reconhecido..." during every build. It doesn't affect anything, but it's noise.


