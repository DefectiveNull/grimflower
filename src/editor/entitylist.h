#pragma once

// The entity types the game has, read from entitylist.txt in the project folder. The game writes that
// file from its own entity table every time build.bat builds it, so the editor never keeps a list of
// its own that could go out of date

#include "raylib.h"
#include "json.hpp"
#include "levellights.h"

#include <string>
#include <vector>

struct LevelEntity;

struct EntityParamInfo
{
    std::string name;
    std::string type;          // number, angle, direction, color or radius
    std::string defaultValue;  // As written in the file: "5", or "255 255 255" for colors and vectors
    std::string description;
    bool hasRange;             // The game gave a range for the slider
    float min;
    float max;
};

// The slider range for a single number parameter: from the game if it gave one, or a guess from its type and default
void GetParamRange(const EntityParamInfo &param, float &min, float &max);

struct EntityClassInfo
{
    std::string classname;
    std::string description;
    std::vector<EntityParamInfo> params;

    // How the editor shows it
    std::string shape;         // "box" or "sphere"
    Vector3 mins;
    Vector3 maxs;
    float radius;
    Color color;
    std::string colorParam;    // Take the color from this parameter instead, if set
    float placement;           // How far off a surface new ones go
};

// Reads the list. Returns false if the file is missing or empty, and keeps the list it had
bool LoadEntityList(const char *path);

const std::vector<EntityClassInfo> &GetEntityClassList();
const EntityClassInfo *FindEntityClass(const std::string &classname);
const EntityParamInfo *FindEntityParam(const EntityClassInfo &entityClass, const std::string &name);
const EntityParamInfo *FindEntityParamOfType(const EntityClassInfo &entityClass, const char *type);

// A parameter's default as a level file value: a number, or an array for colors and directions
nlohmann::ordered_json DefaultParamValue(const EntityParamInfo &param);

// An entity's parameter, falling back to the default from the list, then to fallback
float EntityNumberOrDefault(const LevelEntity &entity, const char *name, float fallback);
Vector3 EntityVectorOrDefault(const LevelEntity &entity, const char *name, Vector3 fallback);
Color EntityColorOrDefault(const LevelEntity &entity, const char *name, Color fallback);

// The same, for ApplyLevelLights (src/shared/levellights.h)
const EntityParamReader &GetEntityListParamReader();
