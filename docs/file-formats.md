# File formats

Every file the game and editor read and write. They're all text (JSON or `.cfg`) except lightmaps and textures, so they work with git and can be read and fixed by hand.

- [Level files](#level-files-mapsjson)
- [Lightmaps](#lightmaps-maps_lightmappng)
- [.materials](#materials)
- [entitylist.txt](#entitylisttxt)
- [.cfg files](#cfg-files)
- [Models and textures](#models-and-textures)
- [gamedata.zip](#gamedatazip)

## Level files (`maps/*.json`)

A level is one JSON file in `maps/`, holding its settings, its entities and its geometry. The editor writes it, and the game loads it with `map <name>`. Numbers are rounded to 4 decimals when saved, so the files stay readable and diff well.

```json
{
    "format": "rlib-level",
    "version": 2,
    "info": {
        "name": "Dev",
        "created": "2026-10-04T09:35:54Z",
        "saved": "2026-10-14T20:01:30Z",
        "skybox": "textures/sky/kloppenheim_06_2k.hdr",
        "lightmapKey": "d8240d2dea861d8c",
        "luxelSize": 16.0,
        "lightmapFilter": "bilinear",
        "entityCount": 5
    },
    "entities": [ ... ],
    "geometry": { "shapes": [ ... ], "meshes": [ ... ] }
}
```

| Key | |
| --- | --- |
| `format` | Always `"rlib-level"`. Files without it aren't loaded |
| `version` | `2`. Version 1 files used an older world unit (32 Quake units each), and are converted when they load |

### info

| Key | |
| --- | --- |
| `name` | The level's name. Defaults to the file name |
| `created`, `saved` | When it was first and last saved, in UTC. Set by the editor |
| `skybox` | The sky's environment image, as a game data path. Empty for a plain background |
| `lightmapKey` | A fingerprint of what the saved lightmap was baked from. Written by the editor. If the level no longer matches it, the lightmap isn't used and a new one is baked |
| `luxelSize` | World units per lightmap texel. Default 16 |
| `lightmapFilter` | `"bilinear"` (smooth) or `"nearest"` (blocky) |
| `entityCount` | How many entities the file has. Written on save, for reference |

### entities

Each entity is an object with a `classname`, an `origin` as `[x, y, z]`, and its parameters as more keys:

```json
{
    "classname": "light",
    "origin": [80.0, 80.0, -25.6],
    "color": [255, 150, 60],
    "intensity": 1.5,
    "radius": 160.0
}
```

See [Entities](entities.md) for every classname and its parameters. Missing parameters use their defaults. Unknown classnames and parameters are skipped, with a warning.

### geometry

The level's geometry is a list of **shapes** and a list of **meshes**. A shape is a solid; a mesh places a shape in the level. Several meshes can use the same shape (instances).

```json
"geometry": {
    "shapes": [
        {
            "vertices": [[-64, -32, -64], [64, -32, -64], ...],
            "faces": [
                {
                    "corners": [0, 1, 3, 2],
                    "material": "textures/dev/wall128.tga",
                    "uAxis": [1, 0, 0],
                    "vAxis": [0, 0, 1],
                    "offset": [0, 0],
                    "scale": [1, 1]
                }
            ]
        }
    ],
    "meshes": [
        { "shape": 0, "position": [0, 32, 0], "rotation": [0, 0, 0, 1] }
    ]
}
```

**Shapes:**

- `vertices`: points relative to the shape's own origin.
- `faces`: flat polygons. Each face is kept whole as one polygon, however many triangles it takes to draw.
  - `corners`: indices into `vertices`, going **counter-clockwise seen from outside**. That's what decides which way a face faces. A shape whose faces all face inwards is a room: it's solid outside its faces.
  - `material`: the face's texture, as a game data path. Its `.materials` entry applies too.
  - `uAxis`, `vAxis`, `offset`, `scale`: how the texture lies on the face, the way the Valve 220 map format does it (see below).

**Meshes:**

- `shape`: index into `shapes`.
- `position`: where the shape's origin goes in the level. A mesh turns around this point.
- `rotation`: a quaternion, `[x, y, z, w]`.

**Texture alignment.** Textures are world aligned, like Quake's and Hammer's. A point on a face gets the texture coordinate (in texels):

```
s = dot(point, uAxis) / scale.x + offset.x
t = dot(point, vAxis) / scale.y + offset.y
```

`uAxis` and `vAxis` are directions in world space, `scale` is world units per texel, and `offset` is in texels. Moving a mesh slides it under its texture, and neighbouring faces with the same axes line up seamlessly. The default, which new faces get, picks the axes from whichever world axis the face is closest to facing, with scale 1 and no offset.

**Collision** isn't stored: the game and the editor work it out from the geometry when a level loads, splitting each mesh into convex pieces.

**Older levels.** A level with no `geometry` but a `"world": { "model": "model/dev.obj", "scale": 1 }` section is from before the editor had geometry tools. Its model is imported as meshes when it loads, and saving it in the editor stores those meshes.

## Lightmaps (`maps/*_lightmap.png`)

The bounce lightmap baked for a level, saved by the editor next to it: `maps/dev.json` has `maps/dev_lightmap.png`. It's a PNG with four layers stacked top to bottom: the bounce light reaching each texel head on, then from each of three tilted directions, which bump mapped materials use (Half-Life 2's radiosity normal mapping).

It's only used when the level's `info.lightmapKey` matches the level as it is now. If the geometry, its textures, the luxel size or the sun changed since, the game bakes a new one in the background instead. Saving in the editor bakes and saves a fresh one.

## .materials

One file at the root of the project, written by the editor's [Materials tab](editor.md#materials) and read by the game when it loads a level. Each material is named after its diffuse texture's path:

```json
{
    "format": "grimflower-materials",
    "version": 1,
    "materials": {
        "textures/dev/wall128.tga": {
            "bumpmap": "textures/dev/wall128_normal.png",
            "specularmap": "textures/dev/wall128_spec.png",
            "specular": 1.0,
            "glossmap": "textures/dev/wall128_gloss.png",
            "gloss": 0.5,
            "reflection": 0.25
        }
    }
}
```

| Key | Default | |
| --- | --- | --- |
| `bumpmap` | none | Tangent space normal map. Green points down the texture (DirectX style) |
| `bumpmapOpenGL` | false | `true` if the bump map's green points up (OpenGL style, like Blender bakes) |
| `specularmap` | none | How much shine each texel has, and its tint |
| `specular` | 0 | How shiny the surface is, times the specular map |
| `glossmap` | none | How tight the shine is per texel: black is a wide sheen, white a sharp highlight |
| `gloss` | 0.5 | 0 to 1, times the gloss map |
| `reflection` | 0 | 0 to 1: how much of the nearest `env_cubemap` it mirrors, masked by the specular map |

Textures with no entry, or only defaults, have no bump and no shine. The editor leaves those out of the file.

## entitylist.txt

Every entity type the game has, for the editor. `build.bat` writes it by running `game.exe -exportentities` after building the game, so **don't edit it**: change the table in `src/game/entities.cpp` instead (see [Adding an entity type](entities.md#adding-an-entity-type)).

```
entity light "Point light with a color and a radius. Up to 32 light the level, and all of them cast shadows"
    marker sphere 6
    color param color
    placement 32
    param color color "255 255 255" "Light color"
    param intensity number "1" "Brightness right next to the light" 0 10
    param radius radius "160" "Distance where the light has faded out completely" 0 1000
end
```

| Line | |
| --- | --- |
| `entity <classname> "<description>"` | Starts an entity type |
| `marker box <min x y z> <max x y z>` | Drawn as a box around its origin |
| `marker sphere <radius>` | Drawn as a sphere |
| `marker volume` | Drawn as its own box, from its `mins` and `maxs` parameters |
| `color <r g b>` or `color param <name>` | Its marker's color, fixed or from one of its color parameters |
| `placement <distance>` | How far off a surface the editor places new ones |
| `param <name> <type> "<default>" "<description>" [<min> <max>]` | A parameter. min and max are the editor's slider range, for numbers |
| `end` | Ends the entity type |

## .cfg files

Configs are console commands, one per line or separated by `;`, with `//` comments and `"quotes"` around arguments with spaces in them.

```
// Movement
bind w "+forward"
bind space "+jump"
sensitivity "3"
```

| File | Written by | |
| --- | --- | --- |
| `cfg/default.cfg` | You | The game's default binds. Runs first, every time the game starts |
| `config.cfg` (next to `game.exe`) | The game | Your binds and options, from the Options menu and `cfg_save` |
| `autoexec.cfg` (next to `game.exe`) | You | Your own commands, run last |
| `editor.cfg` (next to `editor.exe`) | The editor | The editor's binds and `ed_` settings. Edits made while the editor is open are picked up when you save the file. Editor binds can have `ctrl+`, `shift+` and `alt+` in front of a key, like `bind "ctrl+s" "save_level"` |

Key names are letters and numbers, plus `space`, `tab`, `enter`, `escape`, `backspace`, `capslock`, `uparrow`, `downarrow`, `leftarrow`, `rightarrow`, `shift`, `ctrl`, `alt`, `ins`, `del`, `home`, `end`, `pgup`, `pgdn`, `f1` to `f12`, `semicolon`, `mouse1` to `mouse5`, `mwheelup` and `mwheeldown`.

See [The game](game.md#commands) for the game's commands and cvars, and [The editor](editor.md#settings-and-key-binds) for the editor's.

## Models and textures

- **Models** are Wavefront `.obj` files in `model/`, with a `.mtl` file giving each part its texture. They're used for props (`prop_model`) and for importing as level geometry (Edit > Import OBJ as Meshes). Make them in Quake units.
- **Textures** go in `textures/`, in subfolders for categories. `.tga`, `.png`, `.jpg` and `.bmp` all work. Name maps for other textures with `_normal`, `_bump`, `_spec`, `_specular`, `_gloss` or `_glossy` at the end, so the Materials tab doesn't list them as textures of their own.
- **Skyboxes** are environment images, like the `.hdr` in `textures/sky/`, set in the editor's Global World Settings.

## gamedata.zip

`build.bat` packs the game's files into `build/gamedata.zip`, keeping their folders:

- `cfg/*.cfg`
- `maps/*.json`, and `maps/*_lightmap.png` if there are any
- `model/*.obj` and `model/*.mtl`
- `textures/`
- `.materials`, if it exists
- `sound/` and `music/`, once they exist

The game reads everything through it, by the same paths. When `game.exe` runs from inside the project (as `build/game.exe` does), the project's own loose files win over the zip, so changes show up without rebuilding. The editor always uses the project folder.
