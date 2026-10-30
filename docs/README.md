# Grimflower documentation

Grimflower is two programs built from one codebase:

- **The game** (`build/game.exe`): a first person game with Quake 1 style movement, played in levels made with the editor.
- **The editor** (`build/editor.exe`): a level editor where you build a level's geometry, place entities, set up materials and bake its lighting.

They share the engine (`src/engine/`) and the level format (`src/shared/`), so the editor shows a level lit the same way the game draws it.

For building from source, see the [main README](../README.md).

## Pages

| Page | What's in it |
| --- | --- |
| [The editor](editor.md) | Every part of the editor: the camera, Object mode and entities, Modelling mode and its geometry tools, Texturing mode, materials, lighting, saving, and settings |
| [The game](game.md) | Playing: menus, controls, movement, the console, commands and cvars, config files, and how the game draws a level |
| [Entities](entities.md) | Every entity type, what its parameters do, and how to add a new one |
| [File formats](file-formats.md) | Level files, `.materials`, lightmaps, `entitylist.txt`, `.cfg` files and `gamedata.zip` |

## Things that apply everywhere

### Units and axes

- **World units are Quake units.** The player is 32 units wide and 56 tall, and walks at 192 units a second. A 64 unit doorway is a comfortable door, and 16 is a typical step or wall thickness.
- **Y is up.** Levels are built on the X/Z plane.
- **Angles:** an angle of 0 looks down -Z, and 90 looks down -X. This is the same for the player start, props, and the editor camera.

### Game data paths

Files are referred to by their path from the project folder, with forward slashes, like `textures/dev/wall128.tga` or `maps/dev.json`. That's how level files, `.materials` and entity parameters name them. The game finds them in the project folder when it runs from inside it, or in `gamedata.zip` otherwise (see [File formats](file-formats.md#gamedatazip)).

### Project layout

| Folder or file | What it holds |
| --- | --- |
| `maps/` | Levels (`.json`) and their baked lightmaps (`<level>_lightmap.png`) |
| `textures/` | Textures, in subfolders by category (`dev/`, `liquids/`, `sky/`). The editor's Materials tab lists them by subfolder |
| `model/` | `.obj` models with their `.mtl` files, for importing as level geometry or placing as props |
| `cfg/default.cfg` | The game's default key binds |
| `.materials` | Bump, specular and gloss maps for each texture, written by the editor's Materials tab |
| `entitylist.txt` | Every entity type, written by the game during the build, read by the editor. Don't edit it |
| `sound/`, `music/` | Sounds and music (not in the project yet; packed into `gamedata.zip` once they exist) |
| `src/engine/` | The engine: rendering, lighting, the world and its collision, game data, the console, cvars and binds |
| `src/game/` | The game: the player, entities, volumes, menus and game commands |
| `src/editor/` | The editor |
| `src/shared/` | What the editor and game share: the level format, level lights, key names |
