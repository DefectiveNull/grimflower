#include "entitylist.h"
#include "level.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

static std::vector<EntityClassInfo> classes;

// Splits a line on whitespace, keeping "quoted text" together. Stops at a // comment outside quotes
static std::vector<std::string> Tokenize(const std::string &line)
{
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && isspace((unsigned char)line[i])) i++;
        if (i >= line.size() || line.compare(i, 2, "//") == 0) break;

        std::string token;
        if (line[i] == '"')
        {
            i++;
            while (i < line.size() && line[i] != '"') token += line[i++];
            i++;
        }
        else
        {
            while (i < line.size() && !isspace((unsigned char)line[i])) token += line[i++];
        }
        tokens.push_back(token);
    }
    return tokens;
}

static float Number(const std::vector<std::string> &tokens, size_t index)
{
    return (index < tokens.size()) ? (float)atof(tokens[index].c_str()) : 0.0f;
}

static unsigned char Channel(const std::vector<std::string> &tokens, size_t index)
{
    float value = Number(tokens, index);
    return (unsigned char)((value < 0.0f) ? 0.0f : (value > 255.0f) ? 255.0f : value);
}

bool LoadEntityList(const char *path)
{
    char *text = LoadFileText(path);
    if (text == nullptr)
    {
        TraceLog(LOG_WARNING, "ENTITYLIST: [%s] Couldn't read it. Build the game with build.bat to write it", path);
        return false;
    }
    std::istringstream file(text);
    UnloadFileText(text);

    // Read into a new list, so a reload that finds nothing keeps the current one
    std::vector<EntityClassInfo> loaded;
    EntityClassInfo *current = nullptr;
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line))
    {
        lineNumber++;
        std::vector<std::string> tokens = Tokenize(line);
        if (tokens.empty()) continue;
        const std::string &keyword = tokens[0];

        if (keyword == "entity" && tokens.size() >= 2)
        {
            EntityClassInfo entityClass;
            entityClass.classname = tokens[1];
            entityClass.description = (tokens.size() >= 3) ? tokens[2] : "";
            entityClass.shape = "sphere";
            entityClass.mins = entityClass.maxs = { 0.0f, 0.0f, 0.0f };
            entityClass.radius = 0.25f;
            entityClass.color = MAGENTA;
            entityClass.placement = 1.0f;
            loaded.push_back(entityClass);
            current = &loaded.back();
        }
        else if (current == nullptr)
        {
            TraceLog(LOG_WARNING, "ENTITYLIST: [%s] Line %d is outside an entity, skipped", path, lineNumber);
        }
        else if (keyword == "end")
        {
            current = nullptr;
        }
        else if (keyword == "marker" && tokens.size() >= 2)
        {
            current->shape = tokens[1];
            if (tokens[1] == "box")
            {
                current->mins = { Number(tokens, 2), Number(tokens, 3), Number(tokens, 4) };
                current->maxs = { Number(tokens, 5), Number(tokens, 6), Number(tokens, 7) };
            }
            else
            {
                current->radius = Number(tokens, 2);
            }
        }
        else if (keyword == "color" && tokens.size() >= 2)
        {
            if (tokens[1] == "param") current->colorParam = (tokens.size() >= 3) ? tokens[2] : "";
            else current->color = { Channel(tokens, 1), Channel(tokens, 2), Channel(tokens, 3), 255 };
        }
        else if (keyword == "placement")
        {
            current->placement = Number(tokens, 1);
        }
        else if (keyword == "param" && tokens.size() >= 4)
        {
            EntityParamInfo param = { tokens[1], tokens[2], tokens[3], (tokens.size() >= 5) ? tokens[4] : "", false, 0.0f, 0.0f };
            if (tokens.size() >= 7)
            {
                param.hasRange = true;
                param.min = Number(tokens, 5);
                param.max = Number(tokens, 6);
            }
            current->params.push_back(param);
        }
        else
        {
            TraceLog(LOG_WARNING, "ENTITYLIST: [%s] Line %d isn't understood, skipped", path, lineNumber);
        }
    }

    if (loaded.empty())
    {
        TraceLog(LOG_WARNING, "ENTITYLIST: [%s] No entity types in it", path);
        return false;
    }

    classes = loaded;
    TraceLog(LOG_INFO, "ENTITYLIST: [%s] %d entity types", path, (int)classes.size());
    return true;
}

const std::vector<EntityClassInfo> &GetEntityClassList()
{
    return classes;
}

const EntityClassInfo *FindEntityClass(const std::string &classname)
{
    for (const EntityClassInfo &entityClass : classes)
    {
        if (entityClass.classname == classname) return &entityClass;
    }
    return nullptr;
}

const EntityParamInfo *FindEntityParam(const EntityClassInfo &entityClass, const std::string &name)
{
    for (const EntityParamInfo &param : entityClass.params)
    {
        if (param.name == name) return &param;
    }
    return nullptr;
}

const EntityParamInfo *FindEntityParamOfType(const EntityClassInfo &entityClass, const char *type)
{
    for (const EntityParamInfo &param : entityClass.params)
    {
        if (param.type == type) return &param;
    }
    return nullptr;
}

nlohmann::ordered_json DefaultParamValue(const EntityParamInfo &param)
{
    if (param.type == "text") return param.defaultValue;

    std::vector<double> numbers;
    std::istringstream values(param.defaultValue);
    double value;
    while (values >> value) numbers.push_back(value);

    if (param.type == "color" || param.type == "direction" || param.type == "vector")
    {
        nlohmann::ordered_json array = nlohmann::ordered_json::array();
        for (double n : numbers)
        {
            if (param.type == "color") array.push_back((int)n);
            else array.push_back(n);
        }
        return array;
    }

    double number = numbers.empty() ? 0.0 : numbers[0];
    if (param.type == "angle" && number == (int)number) return (int)number;
    return number;
}

// The entity's parameter's default from the list, if the list has it
static bool DefaultFor(const LevelEntity &entity, const char *name, nlohmann::ordered_json &value)
{
    const EntityClassInfo *entityClass = FindEntityClass(entity.classname);
    const EntityParamInfo *param = entityClass ? FindEntityParam(*entityClass, name) : nullptr;
    if (param == nullptr) return false;
    value = DefaultParamValue(*param);
    return true;
}

float EntityNumberOrDefault(const LevelEntity &entity, const char *name, float fallback)
{
    nlohmann::ordered_json value;
    if (DefaultFor(entity, name, value) && value.is_number()) fallback = value.get<float>();
    return EntityFloat(entity, name, fallback);
}

Vector3 EntityVectorOrDefault(const LevelEntity &entity, const char *name, Vector3 fallback)
{
    nlohmann::ordered_json value;
    if (DefaultFor(entity, name, value) && value.is_array() && value.size() == 3)
    {
        fallback = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
    }
    return EntityVector(entity, name, fallback);
}

Color EntityColorOrDefault(const LevelEntity &entity, const char *name, Color fallback)
{
    nlohmann::ordered_json value;
    if (DefaultFor(entity, name, value) && value.is_array() && value.size() >= 3)
    {
        fallback = { (unsigned char)value[0].get<int>(), (unsigned char)value[1].get<int>(), (unsigned char)value[2].get<int>(), 255 };
    }
    return EntityColor(entity, name, fallback);
}

// Fallbacks for when entitylist.txt doesn't have the parameter either
static float ListParamNumber(const LevelEntity &entity, const char *name) { return EntityNumberOrDefault(entity, name, 0.0f); }
static Vector3 ListParamVector(const LevelEntity &entity, const char *name) { return EntityVectorOrDefault(entity, name, { 0.0f, -1.0f, 0.0f }); }
static Color ListParamColor(const LevelEntity &entity, const char *name) { return EntityColorOrDefault(entity, name, WHITE); }

const EntityParamReader &GetEntityListParamReader()
{
    static const EntityParamReader reader = { ListParamNumber, ListParamVector, ListParamColor };
    return reader;
}

void GetParamRange(const EntityParamInfo &param, float &min, float &max)
{
    if (param.hasRange)
    {
        min = param.min;
        max = param.max;
        return;
    }

    // No range from the game: angles go all the way round, anything else up to a few times its default
    float defaultValue = (float)atof(param.defaultValue.c_str());
    min = 0.0f;
    if (param.type == "angle") max = 360.0f;
    else max = (defaultValue > 0.0f) ? defaultValue*4.0f : 10.0f;
}
