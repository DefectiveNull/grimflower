#include "level.h"
#include "world.h"
#include "raymath.h"

#include <cmath>
#include <ctime>

// ordered_json keeps keys in the order they were written, so saved files stay readable
using json = nlohmann::ordered_json;

static const char *LEVEL_FORMAT = "rlib-level";
static const int LEVEL_VERSION = 2;  // 2: world units are Quake units (1 was 32 Quake units per world unit)

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

// [x, y]
static Vector2 ReadVector2(const json &value, Vector2 fallback)
{
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) return fallback;
    return { value[0].get<float>(), value[1].get<float>() };
}

// The "geometry" section: shapes (vertices and faces) and the meshes placing them
static void ReadGeometry(const json &section, Geometry &geometry, const char *path)
{
    auto shapes = section.find("shapes");
    if (shapes != section.end() && shapes->is_array())
    {
        for (const json &item : *shapes)
        {
            GeoShape shape;
            auto vertices = item.find("vertices");
            if (vertices != item.end() && vertices->is_array())
            {
                for (const json &v : *vertices)
                {
                    Vector3 p = { 0.0f, 0.0f, 0.0f };
                    ReadVector(v, p);
                    shape.vertices.push_back(p);
                }
            }
            auto faces = item.find("faces");
            if (faces != item.end() && faces->is_array())
            {
                for (const json &f : *faces)
                {
                    GeoFace face;
                    auto corners = f.find("corners");
                    if (corners == f.end() || !corners->is_array()) continue;
                    bool valid = corners->size() >= 3;
                    for (const json &c : *corners)
                    {
                        int index = c.is_number_integer() ? c.get<int>() : -1;
                        valid = valid && index >= 0 && index < (int)shape.vertices.size();
                        face.corners.push_back(index);
                    }
                    if (!valid)
                    {
                        TraceLog(LOG_WARNING, "LEVEL: [%s] Skipped a face with corners that aren't vertices of its shape", path);
                        continue;
                    }
                    face.material = GetString(f, "material", "");
                    if (f.contains("uAxis")) ReadVector(f["uAxis"], face.uAxis);
                    if (f.contains("vAxis")) ReadVector(f["vAxis"], face.vAxis);
                    if (f.contains("offset")) face.offset = ReadVector2(f["offset"], face.offset);
                    if (f.contains("scale")) face.scale = ReadVector2(f["scale"], face.scale);
                    if (face.scale.x == 0.0f) face.scale.x = 1.0f;
                    if (face.scale.y == 0.0f) face.scale.y = 1.0f;
                    shape.faces.push_back(face);
                }
            }
            geometry.shapes.push_back(shape);
        }
    }

    auto meshes = section.find("meshes");
    if (meshes != section.end() && meshes->is_array())
    {
        for (const json &item : *meshes)
        {
            GeoMesh mesh;
            mesh.shape = (int)GetNumber(item, "shape", -1.0f);
            if (mesh.shape < 0 || mesh.shape >= (int)geometry.shapes.size())
            {
                TraceLog(LOG_WARNING, "LEVEL: [%s] Skipped a mesh with no shape", path);
                continue;
            }
            mesh.position = { 0.0f, 0.0f, 0.0f };
            if (item.contains("position")) ReadVector(item["position"], mesh.position);
            auto rotation = item.find("rotation");
            if (rotation != item.end() && rotation->is_array() && rotation->size() == 4)
            {
                // As saved: renormalizing would change it a hair, and the lightmap's fingerprint with it. Only one
                // that's clearly off (edited by hand) is fixed
                Quaternion q = { (*rotation)[0].get<float>(), (*rotation)[1].get<float>(), (*rotation)[2].get<float>(), (*rotation)[3].get<float>() };
                float length = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
                mesh.rotation = (fabsf(length - 1.0f) > 0.01f) ? QuaternionNormalize(q) : q;
            }
            geometry.meshes.push_back(mesh);
        }
    }
}

static json WriteGeometry(const Geometry &geometry)
{
    auto vec3 = [](Vector3 v) { return json::array({ Tidy(v.x), Tidy(v.y), Tidy(v.z) }); };
    json section;
    section["shapes"] = json::array();
    for (const GeoShape &shape : geometry.shapes)
    {
        json item;
        item["vertices"] = json::array();
        for (Vector3 v : shape.vertices) item["vertices"].push_back(vec3(v));
        item["faces"] = json::array();
        for (const GeoFace &face : shape.faces)
        {
            json f;
            f["corners"] = face.corners;
            f["material"] = face.material;
            f["uAxis"] = vec3(face.uAxis);
            f["vAxis"] = vec3(face.vAxis);
            f["offset"] = { Tidy(face.offset.x), Tidy(face.offset.y) };
            f["scale"] = { Tidy(face.scale.x), Tidy(face.scale.y) };
            item["faces"].push_back(f);
        }
        section["shapes"].push_back(item);
    }
    section["meshes"] = json::array();
    for (const GeoMesh &mesh : geometry.meshes)
    {
        json item;
        item["shape"] = mesh.shape;
        item["position"] = vec3(mesh.position);
        Quaternion q = mesh.rotation;
        item["rotation"] = { Tidy(q.x), Tidy(q.y), Tidy(q.z), Tidy(q.w) };
        section["meshes"].push_back(item);
    }
    return section;
}

float EntityFloat(const LevelEntity &entity, const char *key, float fallback)
{
    return GetNumber(entity.properties, key, fallback);
}

bool EntityBool(const LevelEntity &entity, const char *key, bool fallback)
{
    auto it = entity.properties.find(key);
    if (it == entity.properties.end()) return fallback;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number()) return it->get<float>() != 0.0f;
    return fallback;
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

std::string EntityText(const LevelEntity &entity, const char *key, const std::string &fallback)
{
    return GetString(entity.properties, key, fallback);
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
    level.info.lightmapKey = GetString(info, "lightmapKey", "");
    // luxelsPerUnit is how files from before Quake units had it, in luxels per 32 units
    float luxelSize = info.contains("luxelsPerUnit") ? 32.0f/fmaxf(GetNumber(info, "luxelsPerUnit", 2.0f), 0.01f) : 16.0f;
    level.info.luxelSize = Clamp(GetNumber(info, "luxelSize", luxelSize), 1.0f, 128.0f);
    level.info.lightmapFiltered = GetString(info, "lightmapFilter", "bilinear") != "nearest";
    level.info.texelsPerUnit = (GetNumber(info, "texelsPerUnit", 1.0f) >= 1.5f) ? 2 : 1;
    level.info.entityCount = (int)GetNumber(info, "entityCount", 0.0f);

    // The geometry. Levels from before it, with a world model (an OBJ) instead, get it imported as meshes:
    // saving them keeps those. Version 1 levels were also in the old world units, 32 Quake units each
    int version = (int)GetNumber(root, "version", 1.0f);
    auto geometry = root.find("geometry");
    if (geometry != root.end() && geometry->is_object()) ReadGeometry(*geometry, level.geometry, path);
    else
    {
        const json &world = Section(root, "world");
        std::string model = GetString(world, "model", "");
        float scale = GetNumber(world, "scale", 1.0f)*((version < 2) ? 32.0f : 1.0f);
        if (!model.empty())
        {
            if (ImportObjGeometry(model.c_str(), scale, (float)level.info.texelsPerUnit, level.geometry))
            {
                GeoQuantize(level.geometry);  // As saving will store it
                TraceLog(LOG_INFO, "LEVEL: [%s] Imported its world model %s as meshes", path, model.c_str());
            }
            else TraceLog(LOG_WARNING, "LEVEL: [%s] Couldn't import its world model %s", path, model.c_str());
        }
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

    // Version 1 levels are in the old world units, 32 Quake units each: scaled up to Quake units, which
    // world units are now. The parameters that are distances are scaled with the origins
    if (version < 2)
    {
        for (LevelEntity &entity : level.entities)
        {
            entity.origin = Vector3Scale(entity.origin, 32.0f);
            for (const char *name : { "mins", "maxs", "radius", "lip", "speed", "fade", "scale" })
            {
                auto it = entity.properties.find(name);
                if (it == entity.properties.end()) continue;
                if (it->is_number()) *it = Tidy(it->get<float>()*32.0f);
                else if (it->is_array()) for (json &n : *it) if (n.is_number()) n = Tidy(n.get<float>()*32.0f);
            }
        }
        TraceLog(LOG_INFO, "LEVEL: [%s] Converted from the old world units to Quake units. Saving it keeps it that way", path);
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
    if (!level.info.lightmapKey.empty()) root["info"]["lightmapKey"] = level.info.lightmapKey;
    root["info"]["luxelSize"] = level.info.luxelSize;
    root["info"]["lightmapFilter"] = level.info.lightmapFiltered ? "bilinear" : "nearest";
    root["info"]["texelsPerUnit"] = level.info.texelsPerUnit;
    root["info"]["entityCount"] = level.info.entityCount;

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

    // Last, since it's long: the entities stay near the top, easy to read
    root["geometry"] = WriteGeometry(level.geometry);

    std::string text = CollapseNumberArrays(root.dump(4)) + "\n";
    if (!SaveFileText(path, text.c_str())) return false;

    TraceLog(LOG_INFO, "LEVEL: [%s] Saved \"%s\", %d entities", path, level.info.name.c_str(), level.info.entityCount);
    return true;
}
