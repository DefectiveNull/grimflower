#pragma once

// Copied from src/game/level.h. The editor version sets up lights without spawning a player.
// Level files: JSON files in maps/ that say which world model to use, where the entities are,
// and some map-wide settings. See maps/dev.json for an example.

#include "raylib.h"
#include "json.hpp"
#include "lighting.h"

#include <string>
#include <vector>


// Map-wide settings, the "info" section
struct LevelInfo
{
    std::string name;
    std::string created;   // UTC, like "2026-10-04T09:35:54Z". Set on the first save if missing
    std::string saved;     // UTC, updated on every save
    std::string skybox;    // Environment map for the sky, as a game data path. Empty uses the plain background
    int entityCount;       // How many entities the file has. Updated on every save
};

// Something placed in the level. Like Quake's entities: a classname says what it is, and
// the rest of its parameters depend on the classname:
//   info_player_start   angle (degrees: 0 looks down -Z, 90 down -X)
//   sun                 direction [x, y, z], color [r, g, b], intensity, ambient. origin only places its marker
//   light               color [r, g, b], intensity, radius
struct LevelEntity
{
    std::string classname;        // "info_player_start", "sun" or "light"
    Vector3 origin;               // Position in world units
    nlohmann::ordered_json properties;  // Every other parameter, like "color" or "radius"
};

struct Level
{
    LevelInfo info;
    std::string worldModel;   // OBJ used for both drawing and collision, as a game data path
    float worldScale;         // Converts the OBJ's units to world units
    std::vector<LevelEntity> entities;
};

// Reads a level file through the game data, so it can come from gamedata.zip
bool LoadLevel(Level &level, const char *path);

// Writes a level file to disk, updating info.saved, info.entityCount and, if missing, info.created
bool SaveLevel(Level &level, const char *path);

// Rebuilds the sun and point lights from the level's entities. The editor calls this every frame,
// so edits to light entities show up right away. Without a "sun" entity, defaultSun is used
void ApplyLevelLights(const Level &level, Lighting &lighting, const Sun &defaultSun);

// Reading entity parameters, with a fallback when a parameter is missing or the wrong type
float EntityFloat(const LevelEntity &entity, const char *key, float fallback);
Vector3 EntityVector(const LevelEntity &entity, const char *key, Vector3 fallback);
Color EntityColor(const LevelEntity &entity, const char *key, Color fallback);
