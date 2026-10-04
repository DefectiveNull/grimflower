#pragma once

// Every entity type the game knows, in one table (entities.cpp). The editor learns about them from
// entitylist.txt, which build.bat writes from this table by running game.exe -exportentities.
// To add an entity type, add it to the table: the game and the editor both pick it up from there.

#include "raylib.h"

#include <string>
#include <vector>

struct LevelEntity;
struct Player;
struct World;

// A parameter an entity can have in a level file. The type tells the editor how to show and edit it:
//   number      A plain number
//   angle       Degrees around the vertical axis, 0 looks down -Z. Drawn as a facing arrow, and can be rotated
//   direction   [x, y, z]. Drawn as an arrow, and can be rotated
//   color       [r, g, b], from 0 to 255
//   radius      A distance, drawn as a sphere around the entity
struct EntityParam
{
    const char *name;
    const char *type;
    const char *defaultValue;  // Written like in entitylist.txt: "5", or "255 255 255" for colors and vectors
    const char *description;
    float min;                 // Range for the editor's slider, for single numbers. Leave both 0 to let the editor pick
    float max;
};

// How the editor shows an entity. Only the editor uses this
struct EntityMarker
{
    const char *shape;         // "box" (mins to maxs around the origin) or "sphere" (radius)
    Vector3 mins;
    Vector3 maxs;
    float radius;
    Color color;
    const char *colorParam;    // Or the name of a color parameter to take the color from, or nullptr
    float placement;           // How far off a surface the editor puts new ones
};

// Things that spawning one entity can affect
struct SpawnContext
{
    World &world;
    Player &player;
    bool hasPlayerStart;
    bool hasSun;
};

struct EntityClass
{
    const char *classname;
    const char *description;
    std::vector<EntityParam> params;
    EntityMarker marker;
    void (*spawn)(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context);
};

const std::vector<EntityClass> &GetEntityClasses();
const EntityClass *FindEntityClass(const std::string &classname);

// An entity's parameter, or its default from the table if the level file doesn't set it
float EntityParamNumber(const LevelEntity &entity, const EntityClass &entityClass, const char *name);
Vector3 EntityParamVector(const LevelEntity &entity, const EntityClass &entityClass, const char *name);
Color EntityParamColor(const LevelEntity &entity, const EntityClass &entityClass, const char *name);

// Writes the table as entitylist.txt for the editor. Returns false if the file couldn't be written
bool ExportEntityList(const char *path);
