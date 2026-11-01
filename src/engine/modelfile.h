#pragma once

#include "raylib.h"

#include <string>
#include <vector>

// Reads model files with assimp, through the game data so a model and the files next to it can come from
// gamedata.zip. Only the shapes and diffuse textures are used: skeletons and animations are ignored

struct ModelMaterial
{
    std::string name;
    std::string texturePath;                  // As the file has it, empty if it has none. An embedded texture's is "<model>#<its name>"
    std::vector<unsigned char> embeddedData;  // The image file inside the model, for formats like .glb
};

struct ModelCorner
{
    Vector3 position;
    Vector3 normal;
    Vector2 texcoord;    // With v going down, like textures. 0, 0 if the file has none
    bool hasNormal;
};

struct ModelFace
{
    std::vector<ModelCorner> corners;
    int material;        // Index into ModelFile::materials
};

// One of the file's objects, with its faces already moved to where the file places them
struct ModelObject
{
    std::vector<ModelFace> faces;
};

struct ModelFile
{
    std::vector<ModelMaterial> materials;
    std::vector<ModelObject> objects;
};

// Reads a model. keepPolygons leaves faces as the file has them, for editing; otherwise they're all triangles.
// Returns false if it couldn't be read, and logs why
bool LoadModelFile(const char *path, bool keepPolygons, ModelFile &model);

// Where a model's texture is, as a game data path: the path as written, which is how map editors write them,
// or next to the model, which is how most exporters write them or what's left of a path from someone else's
// computer. Returns "" if it's in neither place
std::string FindModelTexture(const std::string &texturePath, const std::string &modelFolder);
