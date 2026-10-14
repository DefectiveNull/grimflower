We're going to spend today working on geometry and texturing tools inside the editor today. First things first we're going to have to adjust or sizing. we've been downscaling everything so that out imported .obj files can use quake sizing. we're going to abandon that idea and convert all our maps to that quake sizing. And that also means we're getting read of the "world layer" abstraction we have. Instead of that every mesh that is created in the editor is treated as part of the world, and has it's collision calculated the same way as before. It's also affected by dynamic lights and the sun the same way. And yes thid does mean update the gamecode to account for this.

we're going to reference how Unreal 5's Scythe plugin and source 2's hammer editor do geo editing and texturing. So if there's any doubt on how to implement anything that's talked about in this file. Reference how those editors do it online.

From the Top down 

Editor modes:
    - Object mode: The one that you're in by default, used for editing entities in the world, doesn't let you change level geometry in any way.
    - Edit Mode: Is built specifically for editing world meshes. Everything that i want done to the editors geo capabilities should only be accessable through this mode.

in Edit mode we want to have a couple of tools in our toolbar

Create Cube Tool - (Default Shortcut: B)
    Lets you draw out a cube, very similar to how trenchbroom works except that if you draw it against a wall it'll first let you resize it vertically first. The first click locks in one axis's size, and the second click for the other axis (scythe does this). Whatever material is selected gets applied and after you press enter it creates a new cube mesh separate from any other mesh in the scene

Move Mesh Tool - (Default Mouse click and drag)
    once a mesh is selected, it should show an outline around it, if you then click and drag on it it'll move horizontally. If you then hold alt while doing this it should move vertically.

Duplicate Mesh Tool - (Default Shortcut: CTRL+Click and drag)
    Duplicates a mesh, or more accurately creates an instance of it, dragging does the same thing the move tool does.

Rotate Mesh Tool - (Default Shortcut: R)
    creates a pivot at mesh origin, left and right arrow keys rotate the mesh horizontally in 15 degree increments. Up and down rotate it vertically the same way.

Resize Face Tool - (Default Shortcut: Left Shift)
    when you hold shift and drag your mouse on a face it resizes the mesh in that axis, again like trenchbroom. This should not affect the textures as it should simply reference it's world relative tiling/UVs

Cut Face Tool - (Default Shortcut: C)
    Cuts a line across a meshes face. similar to Scythe's cut tool. when you press C, clicking once in a face will tell the editor which face you're cutting, the second click defines the direction of the cut. Pressing enter confirms. This should not affect the entire mesh, it should only affect the face it was used in.

Extrude Tool - (Default Shortcut: CTRL+Shift)
    Basic blender style extrude. works exactly like that. Move the mouse to set extrude distance.

Extrude with Pivot - The same as exxtrude tool
    While extrude tool is active, pressing P wll bring up a pivot and place it in your cursor, if you want to move it do so right away with the same move controls, once you reselect the face to extrude it will not be editable. Pressing P again removes the pivot. Extruding while the pivot is placed extrudes it "around" the pivot, Scythe and source 2 hammer use this for curved geometry.

Push/Pull Tool - (Default Shortcut: Alt+Shift)
    Works like Sketchup's push/pull tool, scythe and umodeller have it too. You can cut holes through geometry regardless of topology. Controls the same as extruede tool

Edge Tool - (Default Shortcut: E)
    Allows you to select mesh edges. CTRL+Clicking selects multiple. dragging moves the edge vertically, holding alt drags the edge vertically.

Bevel Tool - (Default Shortcut: B)
    Once you have selected edges, Bevel too lets you add a bevel, with controls not unlike blender, moving the mouse changes the size, scrolling adds more steps.

Apply Material Tool - (Default Shortcut: Shift+Left Click)
    Lets you apply a Material to a face highligted. Uses the world Aligned texture coordinates. Doing this to nearby faces autostitches the UVs like trenchbroom does



