# The game

The game (`build/game.exe`) is a first person game with Quake 1 style movement. Right now it's a place to walk around the levels made in the editor: there's movement, swimming, ladders, doors, level changes and ambient sound, but no enemies, weapons or other gameplay yet.

- [Running the game](#running-the-game)
- [Menus](#menus)
- [Controls](#controls)
- [Movement](#movement)
- [What's in a level](#whats-in-a-level)
- [How a level is drawn](#how-a-level-is-drawn)
- [The console](#the-console)
- [Commands](#commands)
- [Cvars](#cvars)
- [Config files](#config-files)
- [Game data](#game-data)

## Running the game

Run `build\game.exe`. It opens on the main menu. **Play** loads `maps/dev.json`.

Commands can be given on the command line, Quake style: each `+` starts a command, and the words after it are its arguments. They run after the config files.

```
game.exe +map platforms
game.exe +map rad +r_ssao 0
```

`game.exe -exportentities [file]` writes the entity table to `entitylist.txt` (or the given file) for the editor, and quits without opening a window. `build.bat` runs this after every build.

The game writes `config.cfg` (your binds and options) and `console.log` (see `writelogs`) next to `game.exe`.

## Menus

- **Main menu:** Play, Options, Quit.
- **Pause menu:** Esc during the game. Resume, Options, Quit.
- **Options**, with four tabs:
  - **Video:** resolution, fullscreen, contrast, saturation, film grain and gamma.
  - **Audio:** master, world and music volume.
  - **Game:** mouse sensitivity and invert vertical aim.
  - **Input:** click an action, then press a key to bind it. **Reset to defaults** puts back the binds from `cfg/default.cfg`.

Esc backs out of Options, unless it's waiting for a key to bind, where Esc cancels that instead. Options are saved to `config.cfg`.

## Controls

The default binds, from `cfg/default.cfg`:

| Key | Command | Does |
| --- | --- | --- |
| W / S | `+forward` / `+back` | Move forward / back |
| A / D | `+moveleft` / `+moveright` | Strafe left / right |
| Space | `+jump` | Jump. Swims up in water, flies up while flying |
| Ctrl | `+duck` | Crouch. Swims down in water, flies down while flying |
| X | `fly` | Fly mode on/off |
| F1 | `toggle r_showcolliders` | Shows the collision brushes and every volume |
| Tab | `togglecursor` | Frees the mouse cursor, or captures it again |
| Esc | | Pause menu (not bindable) |
| ` | | Console (not bindable) |

`+moveup` and `+movedown` exist too, for flying, with no keys by default.

Mouse buttons and the wheel can be bound as `mouse1` to `mouse5`, `mwheelup` and `mwheeldown`.

## Movement

Movement follows Quake 1's code (`SV_WalkMove`, `SV_AirAccelerate` and friends), with crouching, swimming and ladders done the way Half-Life does them.

| | Value |
| --- | --- |
| Player box | 32 wide, 56 tall (Quake's hull: -16 -24 -16 to 16 32 16 around the origin) |
| Eye height | 22 above the origin |
| Top speed | 192 units a second |
| Acceleration, friction | 10, 4 (Quake's `sv_accelerate` and `sv_friction`) |
| Gravity | 800 |
| Jump speed | 270 |
| Step height | 18: steps up to this high are walked up |
| Steepest floor | A slope whose normal is under 0.7 up is a wall |

- **Air strafing** works: in the air, only the speed gained along the direction you're pressing is capped, as in Quake.
- **Jumping** needs the jump key let go between jumps.
- **Crouching** halves the player's height and slows them to a third of their speed. Letting go stands back up once there's room above.
- **Swimming:** waist deep or more in a `func_water`, you swim where you look, at 80% of walking speed. Space swims up, Ctrl swims down, and with no input you slowly sink. The screen is tinted blue underwater.
- **Water jump:** swim forward into a wall at the surface, with a ledge up to 40 units above your waist, and you jump out onto it.
- **Ladders:** touching a `func_ladder`, forward climbs up. Looking more than 30 degrees down, it climbs down instead. Jump lets go and pushes you off backwards.
- **Fly mode** (X) turns off gravity: you move where you look, and Space and Ctrl go straight up and down. You still collide with the level.
- **Falling out of the level:** below a height of -1600, you respawn at the player start.

## What's in a level

A level is its **geometry** (every mesh, drawn and collided with) and its **entities**. When a level loads:

- The player spawns at the `info_player_start`, facing its angle. Without one, they're dropped in above the middle of the level.
- The `sun` and `light` entities light it. A level without a sun gets a default one.
- **Water** (`func_water`) is drawn see-through, with its texture warping like Quake's.
- **Ladders** (`func_ladder`) are invisible: put one in front of a ladder in the geometry.
- **Doors** (`func_door`) slide open when you walk into them, wait, then close again.
- **Level changes** (`trigger_changelevel`) load another level when you walk in.
- **Soundscapes** (`trigger_soundscape`) loop a sound while you're inside, fading out towards their sides and panned towards their origin.
- **Props** (`prop_model`) show a model. Static ones are solid (their bounding box) and part of the bounce bake.
- **Cubemaps** (`env_cubemap`) capture the level for reflections.
- **Color correction** (`info_colorcorrection`) sets the level's color palette with a gradient map.

Entities the game doesn't know, and parameters it doesn't use, are skipped with a warning in the console. See [Entities](entities.md) for every type and its parameters.

## How a level is drawn

- **The sun** lights every surface from one direction and casts shadows. Its shadow map is 4096×4096, with hardware filtered soft edges. The `ambient` setting keeps the shadows from going black.
- **Point lights:** up to 32 at once, fading out at their radius. All of them cast shadows (`r_use_pointlight_shadows`).
- **Bounce light:** sunlight bouncing off surfaces onto others, tinted by their color. It's baked into a lightmap. The game loads the lightmap the editor saved with the level (`maps/<level>_lightmap.png`). If there isn't one, or the level's geometry, textures or sun changed since it was baked, the game bakes one in the background, and the bounce light shows up when it's done. Bump maps affect the bounce too, the way Half-Life 2 does it.
- **Materials:** each texture's bump map, specular and gloss maps and reflection, from `.materials` (see [the editor's Materials tab](editor.md#materials)). They apply to the level, doors and props.
- **Reflections** come from the nearest `env_cubemap`s (up to 4 at a time, the ones nearest the camera). They're captured when the level loads, and again when its bounce bake finishes. Lights that move afterwards don't change them.
- **Ambient occlusion** (SSAO) darkens corners and creases (`r_ssao`).
- **The sky** is the level's skybox image, if it has one.
- **Post processing** grades the finished view: the level's color correction (`info_colorcorrection`), then contrast (`r_contrast`), saturation (`r_saturation`), gamma (`r_gamma`) and film grain (`r_filmgrain`). The HUD and menus aren't affected.

## The console

Press **`** (the key under Esc) to open the console, and again or Esc to close it. While it's open, the game ignores your other input. It shows everything the game logs, including warnings about the level.

- Type a command and press Enter. Several can go on one line, separated by `;`.
- **Up** and **Down** go through the commands you've typed before.
- `help` lists every command and cvar.

## Commands

| Command | Does |
| --- | --- |
| `map <name>` | Loads a level: `map dev` loads `maps/dev.json` |
| `fly` | Fly mode on/off |
| `bind <key> "<command>"` | Binds a key. With just a key, shows its bind |
| `unbind <key>`, `unbindall` | Removes a key's bind, or all of them |
| `alias <name> "<command>; <command>"` | Names a list of commands. With no arguments, lists the aliases |
| `toggle <cvar>` | Flips a cvar between 0 and 1 |
| `togglecursor` | Frees or captures the mouse cursor |
| `exec <file>` | Runs the commands in a `.cfg` file |
| `cfg_save [file]` | Saves binds and cvars to `config.cfg`, or the given file |
| `play <file>` | Plays a sound on the world channel, like `play sound/door.wav` |
| `music <file>` / `music stop` | Plays a looping track on the music channel, or stops it |
| `writelogs [path]` | Saves the console log, to `console.log` next to the game by default |
| `about` | Shows the program, library versions and hardware |
| `clear` | Clears the console and its history |
| `help` | Lists every command and cvar |
| `+forward`, `+back`, `+moveleft`, `+moveright`, `+jump`, `+duck`, `+moveup`, `+movedown` | Movement buttons, held while their key is. Each has a `-` command that lets go |

Sounds and music can be `.ogg`, `.mp3`, `.wav`, `.flac` or `.qoa`.

## Cvars

Cvars are settings, set by typing their name and a value, like `r_ssao 0`. Typing just the name shows its value and default.

| Cvar | Default | |
| --- | --- | --- |
| `vid_width`, `vid_height` | 1280, 720 | Window size |
| `vid_fullscreen` | 0 | Fullscreen at that size |
| `sensitivity` | 3 | Mouse look speed: 3 is 0.003 radians per pixel |
| `m_invert` | 0 | Inverted vertical aim |
| `volume` | 1 | Master volume, 0 to 1 |
| `world_volume` | 1 | World sounds, like soundscapes |
| `music_volume` | 0.7 | Music |
| `r_showcolliders` | 0 | Draws the collision brushes and outlines every volume |
| `r_showentities` | 0 | Markers and labels on entities like the sun and lights |
| `r_use_pointlight_shadows` | 1 | Point lights cast shadows |
| `r_ssao` | 1 | Ambient occlusion |
| `r_ssao_radius` | 16 | How far around each point ambient occlusion looks, in world units |
| `r_ssao_direct` | 0.5 | How much ambient occlusion also darkens the sun and lights, 0 to 1 |
| `r_bounce` | 1 | Bounce light. Turning it off hides it straight away; turning it on needs the level reloaded |
| `r_bounce_scale` | 1 | Strength of the bounce light |
| `r_contrast` | 1 | Contrast: more spreads darks and lights apart, less flattens them |
| `r_saturation` | 1 | Color saturation: 0 is grayscale, more is more vivid |
| `r_filmgrain` | 0 | Film grain over the view, 0 for none to 1 for strong |
| `r_gamma` | 1 | Brightness curve: more brightens the darks and midtones, less darkens them. Black and white stay put |

## Config files

When the game starts, it runs these in order, like Quake's `quake.rc`:

1. **`cfg/default.cfg`:** the default binds, from the game data. It starts with `unbindall`.
2. **`config.cfg`** next to the game: your binds and options, written by the Options menu and `cfg_save`.
3. **`autoexec.cfg`** next to the game, if you make one: your own commands and aliases.

Then the command line's `+` commands run.

A `.cfg` file is console commands, one per line (or separated by `;`), with `//` comments:

```
// autoexec.cfg
bind f "fly"
alias lowgfx "r_ssao 0; r_use_pointlight_shadows 0"
r_bounce_scale 1.5
```

## Game data

The game loads its files (levels, models, textures, `.materials`, configs and sounds) by their path from the project folder, like `maps/dev.json`. It looks for them in two places:

- **The project folder**, when `game.exe` is inside it (as `build/game.exe` is). Its loose files win, so saving a level in the editor or changing a texture shows up the next time the level loads, without rebuilding.
- **`gamedata.zip`** next to `game.exe`, which `build.bat` packs. A game shipped without the project folder uses only this.

See [File formats](file-formats.md#gamedatazip) for what goes in the zip.
