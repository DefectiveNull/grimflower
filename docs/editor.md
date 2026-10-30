# The editor

The editor (`build/editor.exe`) builds Grimflower's levels. It edits one level at a time: its geometry (the walls, floors and everything solid), its entities (the player start, lights, doors, water and so on), and some settings for the whole level. Levels are drawn with the game's own lighting, shadows, bounce light and materials, so what you see is what the game shows.

- [Starting the editor](#starting-the-editor)
- [The window](#the-window)
- [Moving the camera](#moving-the-camera)
- [Modes](#modes)
- [Object mode: entities](#object-mode-entities)
- [Modelling mode: geometry](#modelling-mode-geometry)
- [Texturing mode: textures on faces](#texturing-mode-textures-on-faces)
- [Materials](#materials)
- [Lighting and the bounce lightmap](#lighting-and-the-bounce-lightmap)
- [Levels: new, open, save](#levels-new-open-save)
- [Undo](#undo)
- [Menus](#menus)
- [Settings and key binds](#settings-and-key-binds)
- [Keyboard and mouse reference](#keyboard-and-mouse-reference)

## Starting the editor

Run `build\editor.exe`. It has to be inside the project folder: it looks for the folder with `maps/` and `model/` in it, starting from its own folder and going up to three folders higher, and works from there. Every path it saves (textures, models, skyboxes) is relative to that folder.

It also needs `entitylist.txt`, which `build.bat` writes from the game's entity table. Without it, no entities can be created. If you rebuild while the editor is open, it notices the new `entitylist.txt` and loads it.

The editor opens on a new, empty level called "Untitled".

## The window

```
+-----------------------------------------------------------------------+
| File  Edit  View  About           Level name *                       |  Menu bar
+--------------------------------------------------+--------------------+
| [Object|Modelling|Texturing] [tools] Grid 8 [:] | [Details|Materials]|
| tool hint                                        |                    |
|                                                  |   Details panel    |
|                  3D view                         |                    |
|                                                  |                    |
+--------------------------------------------------+--------------------+
| Messages or a reminder of the controls    OBJECT | Grid 8 | 3 meshes ...|  Status bar
+-----------------------------------------------------------------------+
```

- **Menu bar:** File, Edit, View and About menus, and the level's name in the middle. A `*` after the name means there are unsaved changes.
- **3D view:** the level. Its top left has the mode buttons. In Modelling mode the tool buttons are next to them, with a hint for the current tool underneath. In Texturing mode the hint lists its controls. Its top right shows the grid size and the view menu (the three dots).
- **Details panel:** two tabs. **Details** shows the level (its name, file, geometry, the list of entities and the Create Entity buttons), or the selected entity's parameters. **Materials** lists every texture and its material.
- **Status bar:** the latest message for a few seconds, then a reminder of the controls. On the right: the mode, grid size, and how many meshes and entities the level has.

### The view menu

The three dots at the top right of the 3D view pick how the level is drawn:

- **Full 3D:** textures and all the lighting, the way the game draws it.
- **Shaded Unlit:** textures without lighting, for seeing what's what in dark areas.
- **Wireframe:** only the edges of every triangle, without the sky.

## Moving the camera

Hold the **right mouse button** in the 3D view to look around. While it's held:

| Key | Moves |
| --- | --- |
| W / S | Forward / back |
| A / D | Left / right |
| E / Q | Up / down |
| Shift | Three times faster |

The **mouse wheel** moves forward and back, 64 units a notch, without holding anything.

The camera speed (256 units a second by default), field of view and look sensitivity are in Edit > Preferences. These keys are binds, so they can be changed in Preferences > Input too.

## Modes

The editor has three modes. Click one of the buttons at the top left of the view, or press **Tab** to go to the next one: Object, then Modelling, then Texturing, then back to Object.

- **Object mode** is for entities: placing, moving and setting up the player start, lights, doors and the rest. The geometry can't be changed here, but it's complete: the game's collision is worked out and the bounce lightmap bakes.
- **Modelling mode** is for geometry. It's the only place meshes can be made or changed.
- **Texturing mode** is for putting textures on faces and lining them up.

In Modelling and Texturing mode the view is rebuilt quickly after every change, without collision, the lightmap layout or a bounce bake. Those wait until you go back to Object mode, so nothing bakes in the background while you work.

**Levels are saved from Object mode.** Saving in Modelling or Texturing mode switches to Object mode first, then saves. Leaving Modelling mode tidies the geometry up (see [Leaving Modelling mode](#leaving-modelling-mode)), so what's saved is exactly what's built.

## Object mode: entities

Entities are the things placed in a level that aren't geometry. Every type the game has, and what its parameters do, is in [Entities](entities.md).

### Creating an entity

The Details tab (with nothing selected) ends with **Create Entity**: one button per entity type. Hover over one to read what it is. Clicking one places it where the middle of the view meets the level, snapped to the grid across, and lifted a little off the surface so it isn't buried. It's selected straight away, with every parameter set to its default.

If the middle of the view is empty, it's placed 128 units in front of the camera.

### Selecting

- **Left click** an entity's marker in the 3D view, or click it in the **Entities** list in the Details tab. The list scrolls with the mouse wheel.
- **Clicking empty space** or pressing **Esc** deselects it.

Each type has its own marker: a box, a sphere, or for volumes, a see-through box with a small cube at its origin. Lights are drawn in their color, with a wire sphere showing their radius. Entities with an angle get a line showing where they face, and ones with a direction get an orange line pointing that way. Labels over the markers show their class name.

### Moving

- **Drag** a selected entity to move it along the floor, snapped to the grid.
- Hold **Alt** while dragging to move it up and down instead. You can press and let go of Alt during the drag.
- Type exact coordinates in the **Origin** fields of the Details tab.
- **Alt+Q** and **Alt+R** turn it 45 degrees left or right, if it has an angle or a direction.

A drag is one undo step. A click without moving the mouse more than a few pixels only selects, so selecting never nudges anything by accident.

### Resizing volumes

Volumes (`func_water`, `func_ladder`, `func_door`, `trigger_changelevel`, `trigger_soundscape`) are boxes. When one is selected, a white handle sits on the middle of each of its six faces, drawn over everything so the handles inside walls can still be grabbed. **Drag a handle** to move that face in or out, snapped to the grid. A face can't go past the opposite one. Where a handle overlaps the origin cube, the origin wins, so the volume can still be moved.

The volume's box is saved as its `mins` and `maxs` parameters, relative to its origin. You can type those in the Details tab too.

### Editing parameters

The Details tab shows the selected entity: its class, a description, its origin, and every parameter. Hover over a parameter to read what it does. Each type of parameter has its own way of editing it:

| Type | How it's edited |
| --- | --- |
| number, angle, radius | A slider, and a box for typing an exact value. Typed values can go past the slider's range |
| color | A swatch: click it to open the color picker |
| direction | Typed as `[x, y, z]`, or with **Point At**: click Point At, then click a spot on the level, and the direction points from the entity to that spot. Esc cancels |
| vector | Typed as three numbers, like `-64 0 -64` |
| bool | A checkbox |
| text | Typed, like a path or a level name |

Parameters the level doesn't set yet are shown greyed out, with the default the game uses. Changing one adds it. Parameters the game doesn't use (most likely typos) are shown in red.

**Deselect** and **Delete Entity** are at the bottom. **Delete** on the keyboard deletes the selected entity too.

### Props, cubemaps and lights while editing

- `prop_model` entities show their model in the view. Moving one moves the model.
- `light` and `sun` entities light the view as you change them.
- `env_cubemap` entities capture the level a moment after anything changes, so reflections in the editor match the game's.

## Modelling mode: geometry

The level's geometry is made of **meshes**. Each mesh is a solid made of flat faces, with a texture on each face. Meshes can be **instances** of each other: they share one shape, so changing one changes all of them.

In Modelling mode, the toolbar shows the tools, and the line under it explains what the current tool does and which keys it takes. **Everything snaps to the grid**, and each change is one undo step.

### The grid

**[** and **]** step the grid size through 1, 2, 4, 8, 16, 32, 64, 128 and 256 units. The size is shown at the top right of the view and in the status bar. Snapping applies to creating, moving and resizing, in both modes. It can be turned off in Edit > Preferences > View.

New cubes get the texture picked in the [Materials](#materials) tab, or `textures/dev/wall128.tga` with none picked. Textures are changed in [Texturing mode](#texturing-mode-textures-on-faces).

### Tools

| Tool | Key | What it does |
| --- | --- | --- |
| Select | Esc (from another tool) | Selects, moves, duplicates and resizes meshes |
| Cube | B (with no edges selected) | Draws a new box |
| Rotate | R | Turns the selected mesh in 15 degree steps |
| Cut | C | Cuts a face in two |
| Extrude | Ctrl+Shift | Pulls a face out into a new part of the mesh |
| Push/Pull | Alt+Shift | Pulls a face out, or pushes it in to cut a hole |
| Edge | E | Selects, moves and extrudes edges |
| Bevel | B (with edges selected) | Rounds off the selected edges |

Pressing Ctrl and Shift together (in either order) picks Extrude, and Alt and Shift together picks Push/Pull. **Esc** backs out one step at a time: first whatever the tool is in the middle of, then the tool (back to Select), then the selection.

#### Select

- **Click** a mesh to select it. It gets a yellow outline and its origin is shown with red, green and blue axis lines.
- **Drag** a mesh to move it along the floor. Hold **Alt** to move it up and down.
- **Ctrl+drag** a mesh to drag out an instance of it, sharing its shape.
- **Shift+drag** a face to resize the mesh along that face's direction: the face moves in or out.
- **Ctrl+F** flips the selected mesh's normals: it turns inside out. A box becomes a room you can stand inside, with walls 16 units thick. Pressing it again turns it back.
- **Shift+D** deletes the face under the mouse, and every face joined to it on the same flat surface. This is for cleaning up broken meshes. It leaves a hole, so the mesh isn't closed any more. A mesh with no faces left is deleted.
- **Delete** deletes the selected mesh.

#### Cube

1. **Click** on a face or on the grid to start. On a face, the box is drawn on that face's plane.
2. **Move** to draw the rectangle, then **click**.
3. **Move** to pull out its depth, then **click** or press **Enter**.

The new box gets the picked material, world aligned on every face.

#### Rotate

**Click** a mesh to select it. Then **Left** and **Right** turn it around the vertical axis, and **Up** and **Down** turn it around whichever horizontal axis runs most across the view. Each press is 15 degrees. A mesh turns around its origin.

#### Cut

A dot follows the mouse over meshes, showing where a cut would go through, snapped to the grid.

1. **Click** on a face where the cut goes through.
2. **Move** to aim it. It turns in 15 degree steps from the face's texture direction, so straight cuts are easy. **Click** to lock it.
3. **Enter** cuts the face in two. Clicking another face starts over there.

The faces next to the cut face get the new corners too, so the mesh stays closed.

#### Extrude

**Drag** a face out (or in): it moves along its own direction, and new faces join it to where it was, like Blender's extrude.

**Extruding around a pivot**, for curves and arches:

1. Press **P** with the mouse over a spot to place a pivot there (snapped to the grid).
2. Drag the pivot to move it, if needed.
3. **Drag** a face. Instead of moving straight out, it swings around the pivot in 15 degree steps, adding a segment for each step.

Once a face has been extruded around it, the pivot stays put. **P** again removes it.

#### Push/Pull

Like SketchUp's. **Drag a face out** to extrude it. **Drag it in** to cut its outline into the mesh, that deep. Push it in further than the mesh is thick and it cuts a hole right through, like a doorway or a window. The faces of the cut get the face's material.

#### Edge

- **Click** an edge to select it. **Ctrl+click** more edges to add them, or a selected one to deselect it. Clicking empty space deselects them all. Selected edges are orange; the edge under the mouse is white.
- **Drag** a selected edge to move all the selected edges along the floor. Hold **Alt** to move them up and down.
- **Ctrl+drag** a selected edge to extrude the selected edges: copies of them come out where you drag, joined to them by new faces, with the material of the faces they came from. The new edges are selected afterwards, so you can Ctrl+drag again to keep going. This works best on open edges, like the holes Shift+D leaves, where it carries the surface on. On an edge with faces on both sides it makes a thin, one-sided fin.
- **B** bevels the selected edges.

#### Bevel

With edges selected, press **B** (or click the Bevel button).

- **Move the mouse right** to widen the bevel.
- **Scroll** for more steps: 1 step is a flat chamfer, more steps round it off in a circular arc (up to 16).
- **Click** or **Enter** applies it, and **Esc** cancels.

Outside edges (a box's) get rounded off. Inside edges get filled in, like the corners where a doorway's top meets its sides.

**Arched doorway:** in Edge mode, Ctrl+click the two edges that run through the wall at the top corners of the opening. Press **B**, scroll up for 8 or more steps, and move the mouse right. The width stops at half the opening, where the two curves meet in a semicircle. This works on a doorway cut from the bottom of a wall and on a hole all the way through one.

Bevelling has some limits:

- The faces at each end of an edge have to be flat across.
- At an outside corner, the face at each end has to be one piece.
- No two selected edges can share an end.
- The width is capped at what the faces next to the edges allow. Two bevelled edges on the same face can each go up to halfway, so they meet.

If an edge can't be bevelled, the status bar says why and nothing changes.

### Importing OBJ models as geometry

**Edit > Import OBJ as Meshes...** adds an `.obj` file's objects to the level as meshes, with the textures its `.mtl` gives them. It switches to Modelling mode first. Models are expected in Quake units, the same as the level. This is also how levels made before the editor had geometry tools were converted.

### Leaving Modelling mode

Leaving Modelling mode, for either of the other modes:

1. Rounds every vertex to the precision the level file stores, so what's built is exactly what's saved.
2. Joins faces the tools split up back together, where they're on one plane with one texture. Texturing mode then picks whole faces.

Then, back in Object mode:

1. The editor works out **collision**. Each mesh is split into convex pieces the player collides with, like Quake's map compiler does. View > Collision Brushes shows them.
2. It lays out the **lightmap** and starts the bounce bake in the background (see below).

The status bar says how long it took.

## Texturing mode: textures on faces

Texturing mode puts textures on faces and lines them up, like Hammer's face edit mode. It doesn't change the geometry's shape, so faces stay whole: a face here is the whole flat polygon, not the triangles it's drawn with. Going into Texturing mode opens the **Materials** tab, where you pick the texture to put on.

- **Click** a face to select it (orange). **Ctrl+click** adds faces or takes them away. Clicking nothing deselects, and so does **Esc**.
- **Right click** a face to put the picked texture on it. If the face is one of the selected ones, all the selected faces get it. Faces keep their alignment.
- **Shift+right click** a face to put the picked texture on it and **autostitch** it across every face joined to it that had the same texture before. Each face carries on the alignment of the face next to it over the edge they share, wrapping round corners and curves. The whole surface then tiles on from the face you clicked, and that face's own alignment doesn't change. Faces with other textures are left as they are.
- **Shift+Up / Down / Left / Right** justifies the texture on the selected faces, or the one under the mouse if none are selected. The texture's top, bottom, left or right edge lines up with the face's.
- **Shift+E** fits the texture to the face, once across each way.

A quick right click is a click. Holding the right button or moving the mouse with it held looks around, as usual.

Autostitch goes over the gentlest bends first. An arch's steps wrap on from one to the next, so the texture runs unbroken from one side of a doorway, round the arch and down the other side. A closed shape like a box always has a seam somewhere, where the wrap comes back round on itself. Faces only count as joined where they share a whole edge, corner to corner.

## Materials

The **Materials** tab in the details panel lists every texture in `textures/`, grouped by subfolder. Click a folder's name to fold it away. Textures whose names end in `_normal`, `_bump`, `_spec`, `_specular`, `_gloss` or `_glossy` are treated as maps for other textures, and left out of the list. **Refresh** scans the folder again and reloads `.materials`.

Click a texture to **pick** it. That does two things:

- It's the texture right click puts on in Texturing mode, and new cubes get in Modelling mode.
- Its material settings open under it.

Materials work the way they did before PBR, in games like Half-Life 2 and Doom 3. Each texture can have:

| Setting | What it does |
| --- | --- |
| Bump map | A tangent space normal map, so flat faces catch the light like they have detail. Green points down the texture (DirectX style) unless **OpenGL style** is ticked |
| Specular map | How much shine each texel has, and its tint |
| Specular | How shiny the whole surface is, 0 to 2, times the specular map. 0 isn't shiny at all. Setting a specular map turns this to 1 if it was 0 |
| Gloss map | How tight the shine is per texel, from a wide sheen (black) to a sharp highlight (white) |
| Gloss | 0 to 1, times the gloss map |
| Reflection | 0 to 1: how much of the nearest `env_cubemap` it mirrors. The specular map masks it |

Maps are typed as paths or picked with the **...** button. Letters next to each texture in the list show which parts it has: **B**ump, **S**pecular, **G**loss, **R**eflection.

Changes show in the view right away and are saved to `.materials` in the project folder straight away. The game reads that file when it loads a level, so the game uses the same materials for the level, doors and props. Materials aren't part of undo.

## Lighting and the bounce lightmap

The view is lit the same way the game is:

- **The sun** lights everything from one direction, with shadows. A level without a `sun` entity gets a default one.
- **Point lights** (`light` entities): up to 32 light the level at once, and all of them cast shadows.
- **Bounce light:** sunlight that bounces off surfaces onto others, tinted by their textures. It's baked into a **lightmap**.

In Object mode the bake runs in the background whenever something it depends on changes: the geometry or its textures (when you come back from Modelling or Texturing mode), the sun, or a static prop. It starts once things have been still for a moment, and the lightmap updates when it's done. The status bar says how long it took.

**Saving the level saves the lightmap** next to it, as `maps/<level>_lightmap.png`, so the game doesn't have to bake it. If a bake is still running when you save, saving waits for it.

**Edit > Global World Settings** has two lightmap settings:

- **Luxel size:** world units per lightmap texel: 32, 16, 8 or 4. Smaller is sharper but bakes slower. 16 is what Quake used. Changing it re-bakes.
- **Lightmap filtering:** **Smooth** blends between texels; **Blocky** shows each texel as a square, like software Quake.

The game uses both settings too.

## Levels: new, open, save

| Command | Key | |
| --- | --- | --- |
| New Level | Ctrl+N | An empty level, called "Untitled" |
| Open Level... | Ctrl+O | Opens a level file, from `maps/` by default |
| Save | Ctrl+S | Saves to the level's file, or asks where if it's new |
| Save As... | Ctrl+Shift+S | Saves to a new file |

Opening, starting a new level or exiting with unsaved changes asks whether to save them first. Saving only works in Object mode.

**Edit > Global World Settings** also has the level's **name** (shown in the game's console when it loads) and its **skybox**: an environment image, like the `.hdr` in `textures/sky/`, or empty for a plain background. It also shows when the level was created and last saved, and how many entities and meshes it has. Changes apply when you click **Apply**.

## Undo

**Ctrl+Z** undoes and **Ctrl+Y** (or Ctrl+Shift+Z) redoes. Everything that changes the level is undoable: entities, parameters, geometry, and world settings. Drags and slider drags are one step each.

**Edit > Undo History** lists every step: click one to go straight back or forward to it.

The editor remembers 200 steps by default. That can be changed in Edit > Preferences > General.

## Menus

**File:** New Level, Open Level, Save, Save As, Exit Editor.

**Edit:**

- Undo, Redo, Undo History.
- Create Entity (points you to the Create Entity buttons in the Details tab).
- Import OBJ as Meshes.
- Next Mode.
- Global World Settings.
- Dark Mode.
- Preferences.

**View:** what the 3D view shows. Each item is on or off, and remembered:

- **Grid:** the floor grid.
- **Entity Labels:** class names over entities.
- **Point Entities:** markers for entities that aren't volumes, like lights and the player start. Props hide with them.
- **Volumes:** water, ladders, doors and triggers.
- **Collision Brushes:** the convex pieces the player collides with, drawn over the level.

Hidden entities can't be clicked in the view, but can still be picked from the Entities list.

**About > Editor Info:** the editor's version and the libraries it uses.

## Settings and key binds

**Edit > Preferences** has three tabs:

- **General:** dark mode, and how many undo steps to remember.
- **View:** field of view, camera speed, show grid, snap to grid, show labels.
- **Input:** look sensitivity, invert look, and every key bind. Click **Change** next to a command, then press the key (with Ctrl, Shift or Alt if you like). Binding a key takes it from whatever had it. **Clear** removes a command's key, and **Reset to Defaults** puts back the default binds.

Everything is saved as you change it, in `editor.cfg` next to `editor.exe`. It uses the same format as the game's `.cfg` files:

```
bind "ctrl+s" "save_level"
ed_gridsize "8"
ed_darkmode "1"
```

You can edit it by hand, even while the editor is open: the editor picks up the changes when the file is saved.

### Editor commands

Every command a key can be bound to:

| Command | Default key | Does |
| --- | --- | --- |
| `new_level` | Ctrl+N | New level |
| `open_level` | Ctrl+O | Open level |
| `save_level` | Ctrl+S | Save |
| `save_level_as` | Ctrl+Shift+S | Save as |
| `exit_editor` | | Exit |
| `undo` | Ctrl+Z | Undo |
| `redo` | Ctrl+Y, Ctrl+Shift+Z | Redo |
| `undo_history` | | Opens Undo History |
| `create_entity` | | Points to the Create Entity buttons |
| `delete_entity` | Delete | Deletes the selected entity |
| `deselect` | | Deselects |
| `rotate_left` / `rotate_right` | Alt+Q / Alt+R | Turns the selected entity 45 degrees |
| `grid_smaller` / `grid_bigger` | [ / ] | Steps the grid size |
| `import_obj` | | Import OBJ as meshes |
| `cycle_mode` | | Next mode: Object, Modelling, Texturing (Tab also always works). Its old name, `toggle_edit_mode`, still works |
| `world_settings` | | Opens Global World Settings |
| `toggle_darkmode` | | Dark mode on/off |
| `editor_info` | | Opens Editor Info |
| `+forward`, `+back`, `+moveleft`, `+moveright`, `+moveup`, `+movedown` | W, S, A, D, E, Q | Camera movement while the right mouse button is held |
| `+speed` | Shift | Camera three times faster while held |

### Editor settings

| Setting | Default | |
| --- | --- | --- |
| `ed_darkmode` | 0 | Dark UI |
| `ed_undosteps` | 200 | Undo steps remembered |
| `ed_fov` | 70 | Camera field of view, in degrees |
| `ed_movespeed` | 256 | Camera speed, units per second |
| `ed_sensitivity` | 0.003 | Look speed, radians per pixel |
| `ed_invertlook` | 0 | Moving the mouse up looks down |
| `ed_gridsize` | 8 | Grid size |
| `ed_snap` | 1 | Snap to the grid |
| `ed_showgrid`, `ed_showlabels`, `ed_showpointentities`, `ed_showvolumes` | 1 | View menu items |
| `ed_showcolliders` | 0 | Collision Brushes in the View menu |

## Keyboard and mouse reference

### Anywhere

| Input | Does |
| --- | --- |
| Right mouse (hold) + WASD, Q/E, Shift | Fly the camera |
| Mouse wheel | Move the camera forward and back |
| Tab | Next mode: Object, Modelling, Texturing |
| [ / ] | Grid size |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S | New / open / save / save as |
| Esc | Closes a menu, cancels Point At, backs out of a tool, or deselects |

### Object mode

| Input | Does |
| --- | --- |
| Click | Select an entity |
| Drag | Move it (Alt: up and down) |
| Drag a volume's face handle | Resize the volume |
| Alt+Q / Alt+R | Turn it 45 degrees |
| Delete | Delete it |

### Modelling mode

| Input | Does |
| --- | --- |
| B | Cube tool, or Bevel with edges selected |
| R / C / E | Rotate / Cut / Edge tool |
| Ctrl+Shift / Alt+Shift | Extrude / Push/Pull tool |
| Click | Select a mesh |
| Drag / Alt+drag | Move a mesh along the floor / up and down |
| Ctrl+drag | Drag out an instance |
| Shift+drag a face | Resize the mesh |
| Ctrl+F | Flip the selected mesh's normals |
| Shift+D | Delete the face under the mouse and its flat surface |
| Delete | Delete the selected mesh |
| Arrow keys (Rotate tool) | Turn the selected mesh 15 degrees |
| P (Extrude tool) | Place or remove a pivot |
| Ctrl+click (Edge tool) | Add or remove an edge |
| Ctrl+drag a selected edge | Extrude the selected edges |
| Enter | Finish a cube, cut or bevel |

### Texturing mode

| Input | Does |
| --- | --- |
| Click / Ctrl+click | Select a face / add or take away faces |
| Right click | Put the picked texture on the face, or on the selection if the face is in it |
| Shift+right click | Put the picked texture on the face and autostitch it across the faces joined to it that had the same texture |
| Shift+Up / Down / Left / Right | Justify the texture up / down / left / right |
| Shift+E | Fit the texture to the face |
| Esc | Deselect |
