#include "entities.h"
#include "level.h"
#include "levellights.h"
#include "lighting.h"
#include "player.h"
#include "volumes.h"
#include "world.h"
#include "raymath.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------------------------------------
// Spawning, one function per entity type

static void SpawnPlayerStart(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    if (context.hasPlayerStart)
    {
        TraceLog(LOG_WARNING, "LEVEL: More than one info_player_start, using the first");
        return;
    }

    InitPlayer(context.player, entity.origin, EntityParamNumber(entity, entityClass, "angle")*DEG2RAD);
    context.hasPlayerStart = true;
}

// A volume's box in world space, from its mins and maxs around its origin
static BoundingBox VolumeBox(const LevelEntity &entity, const EntityClass &entityClass)
{
    Vector3 mins = EntityParamVector(entity, entityClass, "mins");
    Vector3 maxs = EntityParamVector(entity, entityClass, "maxs");
    return { Vector3Add(entity.origin, Vector3Min(mins, maxs)), Vector3Add(entity.origin, Vector3Max(mins, maxs)) };
}

// Volumes: what they do comes from the entity type, over the whole box
static void SpawnVolume(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context, int contents)
{
    Volume volume = {};
    volume.box = VolumeBox(entity, entityClass);
    volume.contents = contents;
    context.volumes.volumes.push_back(volume);
}

static void SpawnWater(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    SpawnVolume(entity, entityClass, context, CONTENTS_WATER);
    AddWaterSurface(context.volumes, context.volumes.volumes.back());
}

static void SpawnLadder(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    SpawnVolume(entity, entityClass, context, CONTENTS_LADDER);
}

static void SpawnChangeLevel(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    SpawnVolume(entity, entityClass, context, CONTENTS_TRIGGER);
    context.volumes.volumes.back().map = EntityParamText(entity, entityClass, "map");
    if (context.volumes.volumes.back().map.empty()) TraceLog(LOG_WARNING, "LEVEL: trigger_changelevel has no map to load");
}

static void SpawnDoor(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    AddDoor(context.volumes, VolumeBox(entity, entityClass), EntityParamVector(entity, entityClass, "direction"),
            EntityParamNumber(entity, entityClass, "lip"), EntityParamNumber(entity, entityClass, "speed"),
            EntityParamNumber(entity, entityClass, "wait"), EntityParamText(entity, entityClass, "texture").c_str(),
            context.world.lighting.shader);
}

// sun and light have no spawn function: ApplyLevelLights (src/shared/levellights.cpp) sets them up, the
// same way the editor previews them. These read their parameters with the defaults from the table below

static float TableParamNumber(const LevelEntity &entity, const char *name)
{
    const EntityClass *entityClass = FindEntityClass(entity.classname);
    return entityClass ? EntityParamNumber(entity, *entityClass, name) : EntityFloat(entity, name, 0.0f);
}

static Vector3 TableParamVector(const LevelEntity &entity, const char *name)
{
    const EntityClass *entityClass = FindEntityClass(entity.classname);
    return entityClass ? EntityParamVector(entity, *entityClass, name) : EntityVector(entity, name, { 0.0f, 0.0f, 0.0f });
}

static Color TableParamColor(const LevelEntity &entity, const char *name)
{
    const EntityClass *entityClass = FindEntityClass(entity.classname);
    return entityClass ? EntityParamColor(entity, *entityClass, name) : EntityColor(entity, name, WHITE);
}

// ---------------------------------------------------------------------------------------------
// The table

static const std::vector<EntityClass> ENTITY_CLASSES =
{
    {
        "info_player_start", "Where the player spawns, and which way they face",
        {
            { "angle", "angle", "0", "Degrees around the vertical axis: 0 looks down -Z, 90 down -X", 0.0f, 360.0f },
        },
        // The player's collision hull, so the editor shows how much room it needs
        { "box", { -0.5f, -0.75f, -0.5f }, { 0.5f, 1.0f, 0.5f }, 0.0f, { 0, 228, 48, 255 }, nullptr, 0.8f },
        SpawnPlayerStart,
    },
    {
        "sun", "Directional light with shadows, like sunlight. One per level; without one, the level gets a default sun",
        {
            { "direction", "direction", "0.465 -0.591 0.659", "Which way the light travels", 0.0f, 0.0f },
            { "color", "color", "255 244 214", "Light color", 0.0f, 0.0f },
            { "intensity", "number", "1", "Brightness of a surface facing the sun head on", 0.0f, 4.0f },
            { "ambient", "number", "0.35", "Light every surface gets, so shadows aren't black", 0.0f, 1.0f },
        },
        { "sphere", { 0 }, { 0 }, 0.5f, { 253, 249, 0, 255 }, nullptr, 1.0f },
        nullptr,  // ApplyLevelLights
    },
    {
        "light", "Point light with a color and a radius. The first 4 cast shadows",
        {
            { "color", "color", "255 255 255", "Light color", 0.0f, 0.0f },
            { "intensity", "number", "1", "Brightness right next to the light", 0.0f, 10.0f },
            { "radius", "radius", "5", "Distance where the light has faded out completely", 0.0f, 30.0f },
        },
        { "sphere", { 0 }, { 0 }, 0.2f, WHITE, "color", 1.0f },
        nullptr,  // ApplyLevelLights
    },

    // Volumes: a box from mins to maxs around the origin, doing what its type says over the whole box (volumes.cpp)
    {
        "func_water", "Water the player swims in. Drawn see-through in the game",
        {
            { "mins", "vector", "-2 0 -2", "Corner of the box, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "2 1 2", "Opposite corner of the box, relative to the origin", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 40, 110, 200, 255 }, nullptr, 0.0f },
        SpawnWater,
    },
    {
        "func_ladder", "A ladder the player climbs while touching it. Invisible in the game, so put it in front of a ladder in the world model",
        {
            { "mins", "vector", "-0.5 0 -0.125", "Corner of the box, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "0.5 3 0.125", "Opposite corner of the box, relative to the origin", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 230, 150, 40, 255 }, nullptr, 0.0f },
        SpawnLadder,
    },
    {
        "trigger_changelevel", "Loads another level when the player walks in. Invisible in the game",
        {
            { "mins", "vector", "-1 0 -1", "Corner of the box, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "1 2 1", "Opposite corner of the box, relative to the origin", 0.0f, 0.0f },
            { "map", "text", "", "The level to load, like dev for maps/dev.json", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 240, 200, 40, 255 }, nullptr, 0.0f },
        SpawnChangeLevel,
    },
    {
        "func_door", "A solid door that slides open when the player walks into it, then closes again",
        {
            { "mins", "vector", "-1 0 -0.125", "Corner of the door, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "1 2.5 0.125", "Opposite corner of the door, relative to the origin", 0.0f, 0.0f },
            { "direction", "direction", "0 1 0", "Which way it slides open. It moves its own size that way, minus the lip", 0.0f, 0.0f },
            { "lip", "number", "0.125", "How much of it stays in the frame when open", 0.0f, 1.0f },
            { "speed", "number", "3", "World units per second", 0.5f, 10.0f },
            { "wait", "number", "3", "Seconds it stays open before closing. -1 stays open", -1.0f, 10.0f },
            { "texture", "text", "textures/dev/wall64.tga", "Texture it's drawn with, as a game data path", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 140, 220, 90, 255 }, nullptr, 0.0f },
        SpawnDoor,
    },
};

const std::vector<EntityClass> &GetEntityClasses()
{
    return ENTITY_CLASSES;
}

const EntityClass *FindEntityClass(const std::string &classname)
{
    for (const EntityClass &entityClass : ENTITY_CLASSES)
    {
        if (classname == entityClass.classname) return &entityClass;
    }
    return nullptr;
}

bool SpawnLevel(const Level &level, World &world, Player &player, Volumes &volumes)
{
    if (!LoadWorld(world, level.worldModel.c_str(), level.worldScale)) return false;

    // Each entity type spawns itself, see the table above
    SpawnContext context = { world, player, volumes, false };
    for (const LevelEntity &entity : level.entities)
    {
        const EntityClass *entityClass = FindEntityClass(entity.classname);
        if (entityClass == nullptr)
        {
            TraceLog(LOG_WARNING, "LEVEL: Unknown entity classname \"%s\", ignored", entity.classname.c_str());
            continue;
        }

        // Parameters the game doesn't read are most likely typos
        for (auto &property : entity.properties.items())
        {
            bool known = false;
            for (const EntityParam &param : entityClass->params) known = known || property.key() == param.name;
            if (!known) TraceLog(LOG_WARNING, "LEVEL: %s has a \"%s\" parameter, which it doesn't use", entity.classname.c_str(), property.key().c_str());
        }

        if (entityClass->spawn != nullptr) entityClass->spawn(entity, *entityClass, context);
    }

    // LoadWorld put a default sun above a corner of the level; a sun entity replaces it
    const EntityParamReader tableParams = { TableParamNumber, TableParamVector, TableParamColor };
    if (ApplyLevelLights(level, world.lighting, world.lighting.sun, tableParams) > 0)
    {
        TraceLog(LOG_WARNING, "LEVEL: More than one sun, using the first");
    }

    if (!context.hasPlayerStart)
    {
        // Drop the player in from above the middle of the level
        BoundingBox b = world.bounds;
        TraceLog(LOG_WARNING, "LEVEL: No info_player_start, spawning above the middle of the level");
        InitPlayer(player, { (b.min.x + b.max.x)*0.5f, b.max.y + 2.0f, (b.min.z + b.max.z)*0.5f }, 0.0f);
    }

    if (!level.info.skybox.empty() && !LoadSkybox(world.skybox, level.info.skybox.c_str()))
    {
        TraceLog(LOG_WARNING, "LEVEL: Couldn't load the skybox \"%s\", using the plain background", level.info.skybox.c_str());
    }

    return true;
}

// ---------------------------------------------------------------------------------------------
// Parameters

static const EntityParam *FindParam(const EntityClass &entityClass, const char *name)
{
    for (const EntityParam &param : entityClass.params)
    {
        if (strcmp(param.name, name) == 0) return &param;
    }
    TraceLog(LOG_WARNING, "ENTITIES: %s has no parameter \"%s\" in the entity table", entityClass.classname, name);
    return nullptr;
}

// Up to 4 numbers from a default value like "255 244 214"
static int ParseNumbers(const char *text, float *out, int max)
{
    int count = 0;
    char *end;
    for (const char *p = text; count < max; p = end)
    {
        float value = strtof(p, &end);
        if (end == p) break;
        out[count++] = value;
    }
    return count;
}

float EntityParamNumber(const LevelEntity &entity, const EntityClass &entityClass, const char *name)
{
    const EntityParam *param = FindParam(entityClass, name);
    float fallback = param ? (float)atof(param->defaultValue) : 0.0f;
    return EntityFloat(entity, name, fallback);
}

Vector3 EntityParamVector(const LevelEntity &entity, const EntityClass &entityClass, const char *name)
{
    const EntityParam *param = FindParam(entityClass, name);
    float values[3] = { 0.0f, 0.0f, 0.0f };
    if (param) ParseNumbers(param->defaultValue, values, 3);
    return EntityVector(entity, name, { values[0], values[1], values[2] });
}

Color EntityParamColor(const LevelEntity &entity, const EntityClass &entityClass, const char *name)
{
    const EntityParam *param = FindParam(entityClass, name);
    float values[3] = { 255.0f, 255.0f, 255.0f };
    if (param) ParseNumbers(param->defaultValue, values, 3);
    return EntityColor(entity, name, { (unsigned char)values[0], (unsigned char)values[1], (unsigned char)values[2], 255 });
}

std::string EntityParamText(const LevelEntity &entity, const EntityClass &entityClass, const char *name)
{
    const EntityParam *param = FindParam(entityClass, name);
    return EntityText(entity, name, param ? param->defaultValue : "");
}

// ---------------------------------------------------------------------------------------------
// entitylist.txt

bool ExportEntityList(const char *path)
{
    std::string text =
        "// entitylist.txt: every entity type the game has, for the editor.\n"
        "// Written by game.exe -exportentities every time build.bat builds the game, so don't edit it:\n"
        "// change the entity table in src/game/entities.cpp instead.\n"
        "//\n"
        "// entity <classname> \"<description>\"\n"
        "//     marker box <min x y z> <max x y z>   or   marker sphere <radius>   or   marker volume (its own mins and maxs)\n"
        "//     color <r g b>   or   color param <color parameter>\n"
        "//     placement <how far off a surface new ones go>\n"
        "//     param <name> <type> \"<default>\" \"<description>\" [<min> <max>]   (types: number angle direction color radius vector text)\n"
        "//         min and max are the range of the editor's slider, for single numbers\n"
        "// end\n";

    for (const EntityClass &entityClass : ENTITY_CLASSES)
    {
        const EntityMarker &marker = entityClass.marker;

        text += TextFormat("\nentity %s \"%s\"\n", entityClass.classname, entityClass.description);
        if (strcmp(marker.shape, "box") == 0)
        {
            text += TextFormat("    marker box %g %g %g %g %g %g\n", marker.mins.x, marker.mins.y, marker.mins.z, marker.maxs.x, marker.maxs.y, marker.maxs.z);
        }
        else if (strcmp(marker.shape, "volume") == 0)
        {
            text += "    marker volume\n";
        }
        else
        {
            text += TextFormat("    marker sphere %g\n", marker.radius);
        }
        if (marker.colorParam) text += TextFormat("    color param %s\n", marker.colorParam);
        else text += TextFormat("    color %d %d %d\n", marker.color.r, marker.color.g, marker.color.b);
        text += TextFormat("    placement %g\n", marker.placement);

        for (const EntityParam &param : entityClass.params)
        {
            text += TextFormat("    param %s %s \"%s\" \"%s\"", param.name, param.type, param.defaultValue, param.description);
            if (param.min != param.max) text += TextFormat(" %g %g", param.min, param.max);
            text += "\n";
        }
        text += "end\n";
    }

    return SaveFileText(path, text.c_str());
}
