#pragma once

// Level files: JSON files in maps/ with the level's geometry (its meshes), where the entities are, and
// some map-wide settings. See maps/dev.json for an example.

#include "raylib.h"
#include "json.hpp"
#include "geometry.h"

#include <string>
#include <vector>

// Map-wide settings, the "info" section
struct LevelInfo
{
    std::string name;
    std::string created;   // UTC, like "2026-10-04T09:35:54Z". Set on the first save if missing
    std::string saved;     // UTC, updated on every save
    std::string skybox;    // Environment map for the sky, as a game data path. Empty uses the plain background
    std::string lightmapKey;  // Fingerprint of what maps/<level>_lightmap.png was baked from, see LightmapKey. Written by the editor
    float luxelSize = 16.0f;      // Bounce lightmap resolution: world units per texel (luxel). 16, like Quake
    bool lightmapFiltered = true; // Smooth (bilinear) lightmap, or blocky (nearest) luxels like software Quake
    int texelsPerUnit = 1;        // Texture density at face scale 1: 1 texel per world unit, or 2 for textures with twice the detail
    int entityCount;       // How many entities the file has. Updated on every save
};

// Something placed in the level. Like Quake's entities: a classname says what it is, and the rest
// of its parameters depend on the classname. Every classname and its parameters are in the table in
// src/game/entities.cpp, which build.bat also exports to entitylist.txt for the editor
struct LevelEntity
{
    std::string classname;        // Like "info_player_start", see src/game/entities.cpp
    Vector3 origin;               // Position in world units
    nlohmann::ordered_json properties;  // Every other parameter, like "color" or "radius"
};

struct Level
{
    LevelInfo info;
    Geometry geometry;        // The world: every mesh, drawn and collided with (src/engine/geometry.h)
    std::vector<LevelEntity> entities;
};

// Reads a level file through the game data, so it can come from gamedata.zip
bool LoadLevel(Level &level, const char *path);

// Writes a level file to disk, updating info.saved, info.entityCount and, if missing, info.created
bool SaveLevel(Level &level, const char *path);

// Reading entity parameters, with a fallback when a parameter is missing or the wrong type
float EntityFloat(const LevelEntity &entity, const char *key, float fallback);
Vector3 EntityVector(const LevelEntity &entity, const char *key, Vector3 fallback);
Color EntityColor(const LevelEntity &entity, const char *key, Color fallback);
std::string EntityText(const LevelEntity &entity, const char *key, const std::string &fallback);
bool EntityBool(const LevelEntity &entity, const char *key, bool fallback);  // true/false, or a number: 0 is false
