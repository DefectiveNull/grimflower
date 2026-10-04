#include "level.h"
#include "entities.h"
#include "player.h"
#include "world.h"
#include "raymath.h"

#include <cmath>
#include <ctime>

// ordered_json keeps keys in the order they were written, so saved files stay readable
using json = nlohmann::ordered_json;

static const char *LEVEL_FORMAT = "rlib-level";
static const int LEVEL_VERSION = 1;

static std::string NowUtc()
{
    time_t now = time(nullptr);
    tm utc;
    gmtime_s(&utc, &now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

// Rounds to 4 decimals, so floats get written as 0.465 instead of 0.4650000035762787
static double Tidy(float value)
{
    return std::round(value*10000.0)/10000.0;
}

// Puts arrays of plain numbers on one line, like [2.5, 1.5, 1.5], instead of one number per line
static std::string CollapseNumberArrays(const std::string &text)
{
    std::string result;
    size_t i = 0;
    while (i < text.size())
    {
        size_t close = (text[i] == '[') ? text.find(']', i) : std::string::npos;
        if (close != std::string::npos && text.find_first_not_of("0123456789.-+eE, \r\n", i + 1) == close)
        {
            std::string inside = text.substr(i + 1, close - i - 1);
            std::string collapsed;
            for (char c : inside)
            {
                if (c == '\n' || c == '\r' || c == ' ') continue;
                collapsed += (c == ',') ? std::string(", ") : std::string(1, c);
            }
            result += "[" + collapsed + "]";
            i = close + 1;
        }
        else
        {
            result += text[i++];
        }
    }
    return result;
}

// A section of the file, or an empty object if it's missing
static const json &Section(const json &object, const char *key)
{
    static const json empty = json::object();
    auto it = object.find(key);
    return (it != object.end() && it->is_object()) ? *it : empty;
}

static std::string GetString(const json &object, const char *key, const std::string &fallback)
{
    auto it = object.find(key);
    return (it != object.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

static float GetNumber(const json &object, const char *key, float fallback)
{
    auto it = object.find(key);
    return (it != object.end() && it->is_number()) ? it->get<float>() : fallback;
}

// [x, y, z]
static bool ReadVector(const json &value, Vector3 &out)
{
    if (!value.is_array() || value.size() != 3) return false;
    for (const json &n : value) if (!n.is_number()) return false;
    out = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
    return true;
}

float EntityFloat(const LevelEntity &entity, const char *key, float fallback)
{
    return GetNumber(entity.properties, key, fallback);
}

Vector3 EntityVector(const LevelEntity &entity, const char *key, Vector3 fallback)
{
    auto it = entity.properties.find(key);
    Vector3 result;
    return (it != entity.properties.end() && ReadVector(*it, result)) ? result : fallback;
}

// [r, g, b] or [r, g, b, a], from 0 to 255
Color EntityColor(const LevelEntity &entity, const char *key, Color fallback)
{
    auto it = entity.properties.find(key);
    if (it == entity.properties.end() || !it->is_array() || it->size() < 3 || it->size() > 4) return fallback;
    for (const json &n : *it) if (!n.is_number()) return fallback;

    auto channel = [&](int i) { return (unsigned char)Clamp((*it)[i].get<float>(), 0.0f, 255.0f); };
    return { channel(0), channel(1), channel(2), (it->size() == 4) ? channel(3) : (unsigned char)255 };
}

bool LoadLevel(Level &level, const char *path)
{
    char *text = LoadFileText(path);
    if (text == nullptr) return false;
    json root = json::parse(text, nullptr, false);  // false: no exceptions, gives a "discarded" value on errors instead
    UnloadFileText(text);

    if (root.is_discarded() || !root.is_object())
    {
        TraceLog(LOG_WARNING, "LEVEL: [%s] Not valid JSON", path);
        return false;
    }
    if (GetString(root, "format", "") != LEVEL_FORMAT)
    {
        TraceLog(LOG_WARNING, "LEVEL: [%s] Not a level file, \"format\" should be \"%s\"", path, LEVEL_FORMAT);
        return false;
    }

    level = Level();

    const json &info = Section(root, "info");
    level.info.name = GetString(info, "name", GetFileNameWithoutExt(path));
    level.info.created = GetString(info, "created", "");
    level.info.saved = GetString(info, "saved", "");
    level.info.skybox = GetString(info, "skybox", "");
    level.info.entityCount = (int)GetNumber(info, "entityCount", 0.0f);

    const json &world = Section(root, "world");
    level.worldModel = GetString(world, "model", "");
    level.worldScale = GetNumber(world, "scale", 1.0f);
    if (level.worldModel.empty())
    {
        TraceLog(LOG_WARNING, "LEVEL: [%s] Has no world model", path);
        return false;
    }

    auto entities = root.find("entities");
    if (entities != root.end() && entities->is_array())
    {
        for (const json &item : *entities)
        {
            if (!item.is_object() || GetString(item, "classname", "").empty())
            {
                TraceLog(LOG_WARNING, "LEVEL: [%s] Skipped an entity without a classname", path);
                continue;
            }

            LevelEntity entity;
            entity.classname = item["classname"].get<std::string>();
            entity.origin = { 0.0f, 0.0f, 0.0f };
            if (item.contains("origin")) ReadVector(item["origin"], entity.origin);

            entity.properties = item;
            entity.properties.erase("classname");
            entity.properties.erase("origin");
            level.entities.push_back(entity);
        }
    }

    if (level.info.entityCount != (int)level.entities.size())
    {
        TraceLog(LOG_WARNING, "LEVEL: [%s] info.entityCount says %d, but the file has %d entities",
                 path, level.info.entityCount, (int)level.entities.size());
    }

    TraceLog(LOG_INFO, "LEVEL: [%s] Loaded \"%s\", %d entities", path, level.info.name.c_str(), (int)level.entities.size());
    return true;
}

bool SaveLevel(Level &level, const char *path)
{
    std::string now = NowUtc();
    if (level.info.created.empty()) level.info.created = now;
    level.info.saved = now;
    level.info.entityCount = (int)level.entities.size();

    json root;
    root["format"] = LEVEL_FORMAT;
    root["version"] = LEVEL_VERSION;

    root["info"]["name"] = level.info.name;
    root["info"]["created"] = level.info.created;
    root["info"]["saved"] = level.info.saved;
    root["info"]["skybox"] = level.info.skybox;
    root["info"]["entityCount"] = level.info.entityCount;

    root["world"]["model"] = level.worldModel;
    root["world"]["scale"] = level.worldScale;

    root["entities"] = json::array();
    for (const LevelEntity &entity : level.entities)
    {
        json item;
        item["classname"] = entity.classname;
        item["origin"] = { Tidy(entity.origin.x), Tidy(entity.origin.y), Tidy(entity.origin.z) };
        if (entity.properties.is_object())
        {
            for (auto &property : entity.properties.items()) item[property.key()] = property.value();
        }
        root["entities"].push_back(item);
    }

    std::string text = CollapseNumberArrays(root.dump(4)) + "\n";
    if (!SaveFileText(path, text.c_str())) return false;

    TraceLog(LOG_INFO, "LEVEL: [%s] Saved \"%s\", %d entities", path, level.info.name.c_str(), level.info.entityCount);
    return true;
}

bool SpawnLevel(const Level &level, World &world, Player &player)
{
    if (!LoadWorld(world, level.worldModel.c_str(), level.worldScale)) return false;

    // Each entity type spawns itself, see the table in entities.cpp
    SpawnContext context = { world, player, false, false };
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

        entityClass->spawn(entity, *entityClass, context);
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
