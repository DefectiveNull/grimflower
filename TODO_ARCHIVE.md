# TODO Archive

Sections of [TODO.md](TODO.md) whose tasks are all done, kept for reference.

## Editor

- [X] **Direction gizmo**: `direction` parameters (like the sun's) can only be typed as text. A drag handle in the viewport would help.
- [X] **Move gizmo and snapping**: dragged and created entities snap to the grid, and `[` and `]` change its size, from 1/16 to 8 world units in powers of two. No gizmo, on purpose: dragging stays on a plane (Alt for vertical), which is quicker than axis handles. Snapping can be turned off in Preferences.
- [X] **Preferences dialog**: it's a placeholder. Camera speed and sensitivity can only be changed in `editor.cfg`.
- [X] **`editor.cfg` is overwritten on exit**: the editor now only saves on exit if a change is still waiting to be saved, and picks up hand edits to `editor.cfg` while it's open (`src/editor/hotreload.cpp`), so its next save keeps them.
- [X] **Reload the entity list**: `entitylist.txt` and the level's world model are reloaded when they change on disk (`src/editor/hotreload.cpp`), so rebuilding the game or re-exporting from Blender doesn't need a restart. Textures and `.mtl` files aren't watched yet: use Reload World Model for those.
- [X] **Light preview duplicates game code**: `ApplyLevelLights` in `src/shared/levellights.cpp` now sets up `sun` and `light` for both the game and the editor's preview, each taking parameter defaults from its own copy of the entity table. A new light type goes there, plus its parameters in `src/game/entities.cpp`.
