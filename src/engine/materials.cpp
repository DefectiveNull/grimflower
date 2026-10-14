#include "materials.h"
#include "gamedata.h"
#include "json.hpp"

#include <cmath>

// ordered_json keeps keys in the order they were written, so the file stays readable
using json = nlohmann::ordered_json;

static const char *MATERIALS_FORMAT = "grimflower-materials";
static const int MATERIALS_VERSION = 1;

// Rounds to 4 decimals, so the file reads 0.35 instead of 0.34999999
static double Tidy(float value)
{
    return std::round(value*10000.0)/10000.0;
}

MaterialLibrary LoadMaterialLibrary(const char *path)
{
    MaterialLibrary library;
    if (!GameDataFileExists(path)) return library;  // No materials yet
    char *text = LoadFileText(path);  // Through the game data
    if (text == nullptr) return library;

    json root = json::parse(text, nullptr, false);
    UnloadFileText(text);
    if (root.is_discarded() || !root.is_object())
    {
        TraceLog(LOG_WARNING, "MATERIALS: [%s] Isn't valid JSON, no materials loaded", path);
        return library;
    }

    auto materials = root.find("materials");
    if (materials == root.end() || !materials->is_object()) return library;
    for (auto &item : materials->items())
    {
        const json &value = item.value();
        if (!value.is_object()) continue;
        MaterialDef def;
        def.bumpmap = value.value("bumpmap", "");
        def.specularmap = value.value("specularmap", "");
        def.glossmap = value.value("glossmap", "");
        def.specular = value.value("specular", 0.0f);
        def.gloss = value.value("gloss", 0.5f);
        def.bumpmapOpenGL = value.value("bumpmapOpenGL", false);
        def.reflection = value.value("reflection", 0.0f);
        library[item.key()] = def;
    }
    TraceLog(LOG_INFO, "MATERIALS: [%s] Loaded %d materials", path, (int)library.size());
    return library;
}

bool SaveMaterialLibrary(const MaterialLibrary &library, const char *path)
{
    json root;
    root["format"] = MATERIALS_FORMAT;
    root["version"] = MATERIALS_VERSION;
    root["materials"] = json::object();
    for (const auto &[name, def] : library)
    {
        if (def.IsDefault()) continue;
        json &entry = root["materials"][name];
        if (!def.bumpmap.empty()) entry["bumpmap"] = def.bumpmap;
        if (def.bumpmapOpenGL) entry["bumpmapOpenGL"] = true;
        if (!def.specularmap.empty()) entry["specularmap"] = def.specularmap;
        entry["specular"] = Tidy(def.specular);
        if (!def.glossmap.empty()) entry["glossmap"] = def.glossmap;
        entry["gloss"] = Tidy(def.gloss);
        if (def.reflection > 0.0f) entry["reflection"] = Tidy(def.reflection);
    }
    std::string text = root.dump(4) + "\n";
    return SaveFileText(path, text.c_str());
}

MaterialDef FindMaterial(const MaterialLibrary &library, const std::string &diffusePath)
{
    auto it = library.find(diffusePath);
    return (it != library.end()) ? it->second : MaterialDef();
}
