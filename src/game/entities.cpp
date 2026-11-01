#include "entities.h"
#include "bounce.h"
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

static void SpawnSoundscape(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    std::string sound = EntityParamText(entity, entityClass, "sound");
    if (sound.empty()) TraceLog(LOG_WARNING, "LEVEL: trigger_soundscape has no sound to play");
    AddSoundscape(context.volumes, VolumeBox(entity, entityClass), entity.origin, sound.c_str(),
                  EntityParamNumber(entity, entityClass, "volume"), EntityParamNumber(entity, entityClass, "fade"));
}

// A model placed in the level. Static ones are solid and part of the bounce bake, there for good; others
// are decoration the player walks through, which game code can move later
static void SpawnProp(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    std::string path = EntityParamText(entity, entityClass, "model");
    if (path.empty())
    {
        TraceLog(LOG_WARNING, "LEVEL: prop_model has no model");
        return;
    }
    int model = LoadPropModel(context.world, path);
    if (model < 0) return;  // LoadPropModel said why
    AddProp(context.world, model, entity.origin, EntityParamNumber(entity, entityClass, "angle"),
            EntityParamNumber(entity, entityClass, "scale"), EntityParamBool(entity, entityClass, "static"));
}

// A spot the level is captured from for reflections, once when it loads (CaptureCubemaps in main.cpp)
static void SpawnCubemap(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    int size = (int)Clamp(EntityParamNumber(entity, entityClass, "size"), 16.0f, 1024.0f);
    AddCubemap(context.world, entity.origin, size);
}

// The level's color grade, which the game's post processing applies to the whole view (postfx.h). Where it's
// placed doesn't matter
static void SpawnColorCorrection(const LevelEntity &entity, const EntityClass &entityClass, SpawnContext &context)
{
    if (context.world.colorGrade.gradient.id != 0)
    {
        TraceLog(LOG_WARNING, "LEVEL: More than one info_colorcorrection, using the first");
        return;
    }

    std::string gradient = EntityParamText(entity, entityClass, "gradient");
    if (!LoadColorGrade(context.world.colorGrade, gradient.c_str(), EntityParamColor(entity, entityClass, "shadowColor"),
                        EntityParamColor(entity, entityClass, "highlightColor"), EntityParamNumber(entity, entityClass, "strength")))
    {
        TraceLog(LOG_WARNING, "LEVEL: Couldn't load the gradient map \"%s\", using shadowColor to highlightColor", gradient.c_str());
    }
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

static bool TableParamBool(const LevelEntity &entity, const char *name)
{
    const EntityClass *entityClass = FindEntityClass(entity.classname);
    return entityClass ? EntityParamBool(entity, *entityClass, name) : EntityBool(entity, name, false);
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
        { "box", { -16.0f, -24.0f, -16.0f }, { 16.0f, 32.0f, 16.0f }, 0.0f, { 0, 228, 48, 255 }, nullptr, 25.0f },
        SpawnPlayerStart,
    },
    {
        "sun", "Directional light with shadows, like sunlight. One per level; without one, the level gets a default sun",
        {
            { "direction", "direction", "0.465 -0.591 0.659", "Which way the light travels", 0.0f, 0.0f },
            { "color", "color", "255 244 214", "Light color", 0.0f, 0.0f },
            { "intensity", "number", "1", "Brightness of a surface facing the sun head on", 0.0f, 4.0f },
            { "ambient", "number", "0.35", "Light every surface gets, so shadows aren't black", 0.0f, 1.0f },
            { "ambientColor", "color", "255 255 255", "Color of the ambient light, unless ambientFromSky is on", 0.0f, 0.0f },
            { "ambientFromSky", "bool", "0", "Colors the ambient light with the skybox's average color instead of ambientColor. Without a skybox it's white", 0.0f, 0.0f },
        },
        { "sphere", { 0 }, { 0 }, 16.0f, { 253, 249, 0, 255 }, nullptr, 32.0f },
        nullptr,  // ApplyLevelLights
    },
    {
        "light", "Point light with a color and a radius. Up to 32 light the level, and all of them cast shadows",
        {
            { "color", "color", "255 255 255", "Light color", 0.0f, 0.0f },
            { "intensity", "number", "1", "Brightness right next to the light", 0.0f, 10.0f },
            { "radius", "radius", "160", "Distance where the light has faded out completely", 0.0f, 1000.0f },
        },
        { "sphere", { 0 }, { 0 }, 6.0f, WHITE, "color", 32.0f },
        nullptr,  // ApplyLevelLights
    },
    {
        "env_cubemap", "Captures what the level looks like from here, for materials with a Reflection. Each surface reflects the nearest one. Captured once when the level loads, with the lights where they start",
        {
            { "size", "number", "128", "Pixels along each side of its six images: bigger is sharper", 16.0f, 512.0f },
        },
        { "sphere", { 0 }, { 0 }, 10.0f, { 200, 220, 235, 255 }, nullptr, 32.0f },
        SpawnCubemap,
    },
    {
        "info_colorcorrection", "Sets the level's color palette with a gradient map: each pixel's brightness picks a color along the gradient. One per level, anywhere in it",
        {
            { "gradient", "text", "", "Gradient map image, as a game data path: its left end is the color for black, its right end for white, read along its middle row. The preferred way to set the palette", 0.0f, 0.0f },
            { "strength", "number", "1", "How much the gradient map's color replaces the original: 0 none, 1 fully", 0.0f, 1.0f },
            { "shadowColor", "color", "0 0 0", "Without a gradient image: the dark end of a two color gradient", 0.0f, 0.0f },
            { "highlightColor", "color", "255 255 255", "Without a gradient image: the bright end of a two color gradient", 0.0f, 0.0f },
        },
        { "sphere", { 0 }, { 0 }, 10.0f, { 230, 120, 200, 255 }, nullptr, 32.0f },
        SpawnColorCorrection,
    },

    // Volumes: a box from mins to maxs around the origin, doing what its type says over the whole box (volumes.cpp)
    {
        "func_water", "Water the player swims in. Drawn see-through in the game",
        {
            { "mins", "vector", "-64 0 -64", "Corner of the box, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "64 32 64", "Opposite corner of the box, relative to the origin", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 40, 110, 200, 255 }, nullptr, 0.0f },
        SpawnWater,
    },
    {
        "func_ladder", "A ladder the player climbs while touching it. Invisible in the game, so put it in front of a ladder in the level's geometry",
        {
            { "mins", "vector", "-16 0 -4", "Corner of the box, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "16 96 4", "Opposite corner of the box, relative to the origin", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 230, 150, 40, 255 }, nullptr, 0.0f },
        SpawnLadder,
    },
    {
        "trigger_changelevel", "Loads another level when the player walks in. Invisible in the game",
        {
            { "mins", "vector", "-32 0 -32", "Corner of the box, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "32 64 32", "Opposite corner of the box, relative to the origin", 0.0f, 0.0f },
            { "map", "text", "", "The level to load, like dev for maps/dev.json", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 240, 200, 40, 255 }, nullptr, 0.0f },
        SpawnChangeLevel,
    },
    {
        "func_door", "A solid door that slides open when the player walks into it, then closes again",
        {
            { "mins", "vector", "-32 0 -4", "Corner of the door, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "32 80 4", "Opposite corner of the door, relative to the origin", 0.0f, 0.0f },
            { "direction", "direction", "0 1 0", "Which way it slides open. It moves its own size that way, minus the lip", 0.0f, 0.0f },
            { "lip", "number", "4", "How much of it stays in the frame when open", 0.0f, 32.0f },
            { "speed", "number", "96", "World units per second", 16.0f, 320.0f },
            { "wait", "number", "3", "Seconds it stays open before closing. -1 stays open", -1.0f, 10.0f },
            { "texture", "text", "textures/dev/wall64.tga", "Texture it's drawn with, as a game data path", 0.0f, 0.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 140, 220, 90, 255 }, nullptr, 0.0f },
        SpawnDoor,
    },
    {
        "trigger_soundscape", "Loops a sound while the player is inside, fading out towards the sides, and sounding like it comes from its origin",
        {
            { "mins", "vector", "-128 0 -128", "Corner of the box, relative to the origin", 0.0f, 0.0f },
            { "maxs", "vector", "128 96 128", "Opposite corner of the box, relative to the origin", 0.0f, 0.0f },
            { "sound", "text", "", "Sound to loop, as a game data path, like sound/ambient/wind.ogg", 0.0f, 0.0f },
            { "volume", "number", "1", "How loud it is well inside the box, 0 to 1", 0.0f, 1.0f },
            { "fade", "number", "32", "World units in from the sides over which it fades in, so walking out fades it away", 0.0f, 256.0f },
        },
        { "volume", { 0 }, { 0 }, 0.0f, { 190, 110, 230, 255 }, nullptr, 0.0f },
        SpawnSoundscape,
    },

    // Props
    {
        "prop_model", "A model placed in the level, like a crate or a lamp. Static ones are solid; others are decoration the player walks through",
        {
            { "model", "text", "", "The .obj to show, as a game data path, like models/crate.obj. Its .mtl textures and .materials apply", 0.0f, 0.0f },
            { "angle", "angle", "0", "Degrees around the vertical axis", 0.0f, 360.0f },
            { "scale", "number", "1", "Size, as a multiple of the model's own: 1 for models made in Quake units, like the world", 0.0f, 4.0f },
            { "static", "bool", "1", "Part of the level for good: the player collides with the box around it, and it's in the bounce bake. Off: decoration the player walks through", 0.0f, 0.0f },
        },
        { "box", { -8.0f, -8.0f, -8.0f }, { 8.0f, 8.0f, 8.0f }, 0.0f, { 120, 200, 255, 255 }, nullptr, 0.0f },
        SpawnProp,
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
    SetLightmapSettings(world, level.info.luxelSize, level.info.lightmapFiltered);  // Before building, so the lightmap is laid out once
    CreateWorld(world);
    SetWorldGeometry(world, level.geometry, (float)level.info.texelsPerUnit, true);  // Drawn, collided with, and laid out for the bounce lightmap
    if (level.geometry.meshes.empty()) TraceLog(LOG_WARNING, "LEVEL: Has no geometry, there's nothing to stand on");

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

    // Before the lights, since the sun can take its ambient color from the sky
    if (!level.info.skybox.empty() && !LoadSkybox(world.skybox, level.info.skybox.c_str()))
    {
        TraceLog(LOG_WARNING, "LEVEL: Couldn't load the skybox \"%s\", using the plain background", level.info.skybox.c_str());
    }

    // CreateWorld put a default sun above a corner of the level; a sun entity replaces it
    const EntityParamReader tableParams = { TableParamNumber, TableParamVector, TableParamColor, TableParamBool };
    if (ApplyLevelLights(level, world.lighting, world.lighting.sun, tableParams, world.skybox.averageColor) > 0)
    {
        TraceLog(LOG_WARNING, "LEVEL: More than one sun, using the first");
    }

    // Static props have their part of the bounce lightmap, so it's laid out again with them
    bool hasStaticProps = false;
    for (const WorldProp &prop : world.props) hasStaticProps = hasStaticProps || prop.isStatic;
    if (hasStaticProps) BuildLightmapLayout(world);

    if (!context.hasPlayerStart)
    {
        // Drop the player in from above the middle of the level
        BoundingBox b = world.bounds;
        TraceLog(LOG_WARNING, "LEVEL: No info_player_start, spawning above the middle of the level");
        InitPlayer(player, { (b.min.x + b.max.x)*0.5f, b.max.y + 64.0f, (b.min.z + b.max.z)*0.5f }, 0.0f);
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

bool EntityParamBool(const LevelEntity &entity, const EntityClass &entityClass, const char *name)
{
    const EntityParam *param = FindParam(entityClass, name);
    return EntityBool(entity, name, param ? atof(param->defaultValue) != 0.0 : false);
}

// ---------------------------------------------------------------------------------------------
// entitylist.txt

bool ExportEntityList(const char *path)
{
    std::string text =
        "// entitylist.txt: every entity type the game has, for the editor.\n"
        "// Written by grimflower.exe -exportentities every time build.bat builds the game, so don't edit it:\n"
        "// change the entity table in src/game/entities.cpp instead.\n"
        "//\n"
        "// entity <classname> \"<description>\"\n"
        "//     marker box <min x y z> <max x y z>   or   marker sphere <radius>   or   marker volume (its own mins and maxs)\n"
        "//     color <r g b>   or   color param <color parameter>\n"
        "//     placement <how far off a surface new ones go>\n"
        "//     param <name> <type> \"<default>\" \"<description>\" [<min> <max>]   (types: number angle direction color radius vector text bool)\n"
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
