#pragma once

// Materials, the way games did them before PBR (Source, Doom 3, Quake 4): four parts, each its own texture.
//
//   Diffuse (color map): the surface's color, the texture a face has (or a model's .mtl gives it). A material is
//                        named after its diffuse texture's path, like "textures/dev/wall128.tga"
//   Bump map (normal map): which way each texel faces, so flat faces catch the light like they have detail.
//                        Tangent space, the blue-ish kind. Like Source's $bumpmap: green points down the
//                        texture (DirectX style), unless the material says it's an OpenGL style map
//   Specular map:        how much shine each texel has, and its tint, like Source's $envmapmask/phong mask
//   Gloss map:           how tight that shine is, from a wide sheen (black) to a sharp highlight (white),
//                        like Source's $phongexponenttexture
//   Reflection:          how much of the level it mirrors, from the nearest env_cubemap, like Source's $envmap.
//                        The specular map masks it, like $envmapmask
//
// Every part but the diffuse is optional. They're kept in one file at the root of the game data, .materials,
// which the editor's Materials tab writes, and the game and editor read when a world loads

#include "raylib.h"

#include <map>
#include <string>

static const char *MATERIALS_PATH = ".materials";

struct MaterialDef
{
    std::string bumpmap;       // Game data paths, empty for none
    std::string specularmap;
    std::string glossmap;
    float specular = 0.0f;     // How shiny: 0 isn't at all. The specular map multiplies it
    float gloss = 0.5f;        // 0 to 1, a wide sheen to a sharp highlight. The gloss map multiplies it
    bool bumpmapOpenGL = false;  // The bump map's green points up the texture, the way OpenGL and Blender bake them
    float reflection = 0.0f;   // 0 to 1: how much of the nearest cubemap it reflects. The specular map masks it

    bool IsDefault() const { return bumpmap.empty() && specularmap.empty() && glossmap.empty() && specular == 0.0f && gloss == 0.5f && !bumpmapOpenGL && reflection == 0.0f; }
};

// Every material, by diffuse texture path
typedef std::map<std::string, MaterialDef> MaterialLibrary;

// Reads .materials through the game data, so it can come from gamedata.zip. A missing file is an empty library
MaterialLibrary LoadMaterialLibrary(const char *path = MATERIALS_PATH);

// Writes it, leaving out materials that are all defaults. Returns false if it couldn't be written
bool SaveMaterialLibrary(const MaterialLibrary &library, const char *path = MATERIALS_PATH);

// The material for a diffuse texture, or the defaults (no bump, no shine) if the library doesn't have one
MaterialDef FindMaterial(const MaterialLibrary &library, const std::string &diffusePath);
