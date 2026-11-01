# Entities

Entities are everything placed in a level that isn't geometry. Like Quake's, each one has a **classname** saying what it is, an **origin** (where it is), and **parameters** that depend on its classname.

Every entity type is defined in one table in `src/game/entities.cpp`. `build.bat` exports that table to `entitylist.txt`, which is how the editor knows each type's parameters, defaults, descriptions and marker. So the game and the editor never disagree about what an entity has.

A parameter a level doesn't set uses the default below. Sliders in the editor cover the range shown, but you can type values outside it.

- [Point entities](#point-entities): `info_player_start`, `sun`, `light`, `env_cubemap`, `info_colorcorrection`, `prop_model`
- [Volumes](#volumes): `func_water`, `func_ladder`, `func_door`, `trigger_changelevel`, `trigger_soundscape`
- [Adding an entity type](#adding-an-entity-type)

## Point entities

### info_player_start

Where the player spawns, and which way they face. The editor draws it as the player's collision box, so you can see how much room the player needs.

| Parameter | Type | Default | |
| --- | --- | --- | --- |
| `angle` | angle | 0 | Degrees around the vertical axis: 0 looks down -Z, 90 down -X |

Only the first one counts. Without one, the player is dropped in above the middle of the level.

### sun

Light from one direction with shadows, like sunlight. It also drives the bounce lightmap. Only the first one counts. Without one, the level gets a default sun above a corner of the level. Its origin only sets where its marker is drawn.

| Parameter | Type | Default | Range | |
| --- | --- | --- | --- | --- |
| `direction` | direction | 0.465 -0.591 0.659 | | Which way the light travels. **Point At** in the editor is the easy way to aim it |
| `color` | color | 255 244 214 | | Light color |
| `intensity` | number | 1 | 0–4 | Brightness of a surface facing the sun head on |
| `ambient` | number | 0.35 | 0–1 | Light every surface gets, so shadows aren't black |
| `ambientColor` | color | 255 255 255 | | Color of the ambient light, unless `ambientFromSky` is on |
| `ambientFromSky` | bool | false | | Colors the ambient light with the skybox's average color instead of `ambientColor`. The average is over the upper half of the sky, down to the horizon (so the ground doesn't tint it), with each pixel counting no brighter than white so an HDR sun doesn't swamp it, then brightened to full strength: `ambient` still sets how bright it is. Without a skybox it's white |

### light

A point light with a color, fading out to nothing at its radius. Up to 32 light the level at once, and all of them cast shadows. The editor draws its marker in its color, with a wire sphere for its radius.

| Parameter | Type | Default | Range | |
| --- | --- | --- | --- | --- |
| `color` | color | 255 255 255 | | Light color |
| `intensity` | number | 1 | 0–10 | Brightness right next to the light |
| `radius` | radius | 160 | 0–1000 | Distance where the light has faded out completely |

### env_cubemap

Captures what the level looks like from this spot, in six directions, for materials with a **Reflection** (see [Materials](editor.md#materials)). Each surface reflects the nearest one; up to 4 are used at a time. It's captured when the level loads (and again when its bounce bake finishes), with the lights where they start. Place them where the camera usually is, around the middle of a room.

| Parameter | Type | Default | Range | |
| --- | --- | --- | --- | --- |
| `size` | number | 128 | 16–512 | Pixels along each side of its six images: bigger is sharper |

### info_colorcorrection

Sets the level's color palette with a **gradient map**: each pixel's brightness picks a color along a gradient, from its left end (for black) to its right end (for white). The game applies it to the whole view, before the player's contrast, saturation, gamma and film grain settings. One per level, and where it's placed doesn't matter.

A gradient image is the preferred way to set the palette: any image works, read along its middle row, so a 256×1 strip is plenty. Without one, `shadowColor` and `highlightColor` make a simple two color gradient.

| Parameter | Type | Default | Range | |
| --- | --- | --- | --- | --- |
| `gradient` | text | | | Gradient map image, as a game data path, like `textures/grade/dusk.png` |
| `strength` | number | 1 | 0–1 | How much the gradient map's color replaces the original: 0 none, 1 fully |
| `shadowColor` | color | 0 0 0 | | Without a gradient image: the dark end of a two color gradient |
| `highlightColor` | color | 255 255 255 | | Without a gradient image: the bright end of a two color gradient |

### prop_model

A model placed in the level, like a crate or a lamp. The model is an `.obj`, with the textures its `.mtl` gives it and their materials from `.materials`.

| Parameter | Type | Default | Range | |
| --- | --- | --- | --- | --- |
| `model` | text | | | The `.obj` to show, as a game data path, like `models/crate.obj` |
| `angle` | angle | 0 | 0–360 | Degrees around the vertical axis |
| `scale` | number | 1 | 0–4 | Size, as a multiple of the model's own. 1 for models made in Quake units, like the world |
| `static` | bool | true | | **On:** part of the level for good. The player collides with the box around it, and it's in the bounce bake. **Off:** decoration the player walks through |

In the editor, props hide along with the other point entities (View > Point Entities).

## Volumes

Volumes are boxes, like Half-Life's brush entities. Each has a `mins` and a `maxs` parameter: the box's opposite corners, relative to the origin. In the editor, select one and drag the handles on its faces to resize it. Volumes are invisible in the game, except water and doors. `r_showcolliders 1` (F1) outlines them all.

### func_water

Water the player swims in. It's drawn see-through, with its texture (`textures/liquids/water.tga`) warping like Quake's. Underwater, the screen is tinted blue.

| Parameter | Type | Default | |
| --- | --- | --- | --- |
| `mins` | vector | -64 0 -64 | Corner of the box, relative to the origin |
| `maxs` | vector | 64 32 64 | Opposite corner |

The player swims once they're waist deep. See [Movement](game.md#movement) for swimming and water jumps.

### func_ladder

A ladder the player climbs while touching it. It's invisible, so put it in front of a ladder in the level's geometry.

| Parameter | Type | Default | |
| --- | --- | --- | --- |
| `mins` | vector | -16 0 -4 | Corner of the box, relative to the origin |
| `maxs` | vector | 16 96 4 | Opposite corner |

### func_door

A solid box that slides open when the player walks into it, waits, then closes again. If it would close on the player, it opens again instead of crushing them. It's drawn with its texture, and that texture's material.

| Parameter | Type | Default | Range | |
| --- | --- | --- | --- | --- |
| `mins` | vector | -32 0 -4 | | Corner of the door, relative to the origin |
| `maxs` | vector | 32 80 4 | | Opposite corner |
| `direction` | direction | 0 1 0 | | Which way it slides open. It moves its own size that way, minus the lip |
| `lip` | number | 4 | 0–32 | How much of it stays in the frame when open |
| `speed` | number | 96 | 16–320 | World units per second |
| `wait` | number | 3 | -1–10 | Seconds it stays open before closing. -1 stays open for good |
| `texture` | text | textures/dev/wall64.tga | | Texture it's drawn with, as a game data path |

### trigger_changelevel

Loads another level when the player walks in. Only the first one the player touches fires.

| Parameter | Type | Default | |
| --- | --- | --- | --- |
| `mins` | vector | -32 0 -32 | Corner of the box, relative to the origin |
| `maxs` | vector | 32 64 32 | Opposite corner |
| `map` | text | | The level to load, like `dev` for `maps/dev.json` |

### trigger_soundscape

Loops a sound while the player is inside, like wind in a canyon or the hum of a machine room.

- **It fades** in over `fade` units from the box's sides, so walking out fades it smoothly away.
- **It's panned** towards the entity's origin, so it sounds like it comes from there. Put the origin where the sound should come from: it doesn't have to be in the middle of the box. It's never fully in one ear.

| Parameter | Type | Default | Range | |
| --- | --- | --- | --- | --- |
| `mins` | vector | -128 0 -128 | | Corner of the box, relative to the origin |
| `maxs` | vector | 128 96 128 | | Opposite corner |
| `sound` | text | | | Sound to loop, as a game data path, like `sound/ambient/wind.ogg`. `.ogg`, `.mp3`, `.wav`, `.flac` or `.qoa` |
| `volume` | number | 1 | 0–1 | How loud it is well inside the box |
| `fade` | number | 32 | 0–256 | World units in from the sides over which it fades in |

It plays on the world channel, so `world_volume` and `volume` affect it.

## Adding an entity type

1. Add an entry to `ENTITY_CLASSES` in `src/game/entities.cpp`:
   - Its classname and description. The editor shows the description.
   - Its parameters: name, type, default, description, and a slider range for numbers.
   - Its marker: how the editor draws it (a box, a sphere, or a volume), its color, and how far off a surface new ones are placed.
   - Its spawn function, which sets it up in the game when a level loads.
2. Write the spawn function. Read parameters with `EntityParamNumber`, `EntityParamVector`, `EntityParamColor`, `EntityParamText` and `EntityParamBool`: they fall back to the table's default when the level doesn't set one.
3. Run `build.bat`. It rebuilds the game, exports `entitylist.txt`, and rebuilds the editor. An editor that's already open picks up the new list on its own.

Parameter types and how the editor edits them:

| Type | Written in the level file as | Edited in the editor with |
| --- | --- | --- |
| `number` | `5` | A slider and a box |
| `angle` | `90` (degrees, 0 looks down -Z) | A slider, Alt+Q / Alt+R. Drawn as a facing line |
| `direction` | `[x, y, z]` | Typed, or Point At. Drawn as an arrow; Alt+Q / Alt+R swing it |
| `color` | `[r, g, b]`, 0 to 255 | A color picker |
| `radius` | `160` | A slider. Drawn as a sphere |
| `vector` | `[x, y, z]` | Typed as three numbers |
| `text` | `"textures/dev/wall64.tga"` | Typed |
| `bool` | `true` / `false` | A checkbox |
