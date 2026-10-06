// The engine's console commands and cvars. The game adds its own commands with AddCommands, see
// src/game/gamecommands.cpp.
// To add an engine command: write a Cmd_ function below, then add a line for it to the commands table.
// To add a cvar: define it next to the others, declare it in cvars.h, then add it to the cvars table.

// windows.h has to come before raylib.h, with the parts that clash with raylib's names left out
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #define NOMINMAX
    #include <windows.h>
#endif
#if defined(_MSC_VER)
    #include <intrin.h>
#endif

#include "raylib.h"
#include "raygui.h"
#include "cvars.h"
#include "binds.h"
#include "console.h"
#include "gamedata.h"
#include "keys.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <thread>

static void Cmd_About(const std::vector<std::string> &args);
static void Cmd_Alias(const std::vector<std::string> &args);
static void Cmd_Bind(const std::vector<std::string> &args);
static void Cmd_CfgSave(const std::vector<std::string> &args);
static void Cmd_Clear(const std::vector<std::string> &args);
static void Cmd_Exec(const std::vector<std::string> &args);
static void Cmd_Help(const std::vector<std::string> &args);
static void Cmd_Toggle(const std::vector<std::string> &args);
static void Cmd_ToggleCursor(const std::vector<std::string> &args);
static void Cmd_Unbind(const std::vector<std::string> &args);
static void Cmd_UnbindAll(const std::vector<std::string> &args);
static void Cmd_WriteLogs(const std::vector<std::string> &args);

static const ConsoleCommand commands[] =
{
    { "about", "Shows the program, library versions and hardware specs", Cmd_About },
    { "alias", "Names a list of commands: alias <name> \"<command>; <command>\". No arguments lists aliases", Cmd_Alias },
    { "bind", "Binds a key to a command: bind <key> \"<command>\". Just a key shows its bind", Cmd_Bind },
    { "cfg_save", "Saves binds and cvars to config.cfg next to the game, or cfg_save <file>", Cmd_CfgSave },
    { "clear", "Clears the console text and command history", Cmd_Clear },
    { "exec", "Runs the commands in a .cfg file: exec <file>", Cmd_Exec },
    { "help", "Lists all commands and cvars", Cmd_Help },
    { "toggle", "Flips a cvar between 0 and 1: toggle <cvar>", Cmd_Toggle },
    { "togglecursor", "Frees or captures the mouse cursor", Cmd_ToggleCursor },
    { "unbind", "Removes a key's bind: unbind <key>", Cmd_Unbind },
    { "unbindall", "Removes every bind", Cmd_UnbindAll },
    { "writelogs", "Saves the console log to a file: writelogs [path], default console.log next to the program", Cmd_WriteLogs },
};

// Commands added with AddCommands, after the engine's own
static std::vector<ConsoleCommand> addedCommands;

void AddCommands(const ConsoleCommand *list, int count)
{
    addedCommands.insert(addedCommands.end(), list, list + count);
}

// { name, description, value, default value }
Cvar r_showentities = { "r_showentities", "Shows markers and labels on world entities like the sun (0/1)", 0.0f, 0.0f };
Cvar r_use_pointlight_shadows = { "r_use_pointlight_shadows", "Point lights cast shadows, up to the first 4 lights (0/1)", 1.0f, 1.0f };
Cvar r_showcolliders = { "r_showcolliders", "Draws the level's collision brushes (0/1)", 0.0f, 0.0f };
Cvar r_ssao = { "r_ssao", "Screen space ambient occlusion on the world mesh (0/1)", 1.0f, 1.0f };
Cvar r_ssao_radius = { "r_ssao_radius", "How far around each point ambient occlusion looks for things hemming it in, in world units", 0.5f, 0.5f };
Cvar r_ssao_direct = { "r_ssao_direct", "How much ambient occlusion also darkens light from the sun and lights, 0 to 1", 0.5f, 0.5f };
Cvar r_bounce = { "r_bounce", "Sun bounce light (0/1). Baked when a level loads, so turning it on needs a map reload; turning it off hides it straight away", 1.0f, 1.0f };
Cvar r_bounce_scale = { "r_bounce_scale", "Strength of the baked sun bounce", 1.0f, 1.0f };

static Cvar *const cvars[] =
{
    &r_showentities,
    &r_use_pointlight_shadows,
    &r_showcolliders,
    &r_ssao,
    &r_ssao_radius,
    &r_ssao_direct,
    &r_bounce,
    &r_bounce_scale,
};

// Cvars added with AddCvars, after the engine's own
static std::vector<Cvar *> addedCvars;

void AddCvars(Cvar *const *list, int count)
{
    addedCvars.insert(addedCvars.end(), list, list + count);
}

// The engine's cvars and the added ones, in that order
static std::vector<Cvar *> AllCvars()
{
    std::vector<Cvar *> all(std::begin(cvars), std::end(cvars));
    all.insert(all.end(), addedCvars.begin(), addedCvars.end());
    return all;
}

// alias name -> the commands it runs
static std::map<std::string, std::string> aliases;

static const char *CONFIG_FILE = "config.cfg";  // Written by cfg_save, next to the game
static bool runningConfig = false;               // True while exec runs a .cfg file

static bool NamesMatch(const char *a, const std::string &name)
{
    const char *b = name.c_str();
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

const ConsoleCommand *FindCommand(const std::string &name)
{
    for (const ConsoleCommand &command : commands)
    {
        if (NamesMatch(command.name, name)) return &command;
    }
    for (const ConsoleCommand &command : addedCommands)
    {
        if (NamesMatch(command.name, name)) return &command;
    }
    return nullptr;
}

Cvar *FindCvar(const std::string &name)
{
    for (Cvar *cvar : AllCvars())
    {
        if (NamesMatch(cvar->name, name)) return cvar;
    }
    return nullptr;
}

bool HandleCvar(const std::vector<std::string> &args)
{
    Cvar *cvar = FindCvar(args[0]);
    if (cvar == nullptr) return false;

    if (args.size() > 1)
    {
        cvar->value = (float)atof(args[1].c_str());
        if (!runningConfig) ConsolePrintf("%s set to \"%g\"", cvar->name, cvar->value);  // Configs set cvars quietly, like Quake
    }
    else
    {
        ConsolePrintf("\"%s\" is \"%g\", default \"%g\"", cvar->name, cvar->value, cvar->defaultValue);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Running commands

std::vector<std::string> TokenizeCommand(const std::string &command)
{
    std::vector<std::string> args;
    size_t i = 0;
    while (i < command.size())
    {
        while (i < command.size() && isspace((unsigned char)command[i])) i++;
        if (i >= command.size()) break;

        std::string arg;
        if (command[i] == '"')
        {
            i++;
            while (i < command.size() && command[i] != '"') arg += command[i++];
            i++;  // Skip the closing quote
        }
        else
        {
            while (i < command.size() && !isspace((unsigned char)command[i])) arg += command[i++];
        }
        args.push_back(arg);
    }
    return args;
}

// Splits text into single commands at ; and new lines, leaving out // comments. Both are ignored
// inside quotes, so bind x "a; b" keeps "a; b" together
static std::vector<std::string> SplitCommands(const std::string &text)
{
    std::vector<std::string> commands;
    std::string current;
    bool quoted = false;

    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];
        if (c == '"') quoted = !quoted;

        if (!quoted && c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n') i++;  // Skip to the end of the line
            c = '\n';
        }

        if (!quoted && (c == ';' || c == '\n' || c == '\r'))
        {
            commands.push_back(current);
            current.clear();
            if (c == '\n') quoted = false;
        }
        else
        {
            current += c;
        }
    }
    commands.push_back(current);
    return commands;
}

static int aliasDepth = 0;  // Stops aliases that run themselves from looping forever

void ExecuteCommandText(const std::string &text)
{
    for (const std::string &line : SplitCommands(text))
    {
        std::vector<std::string> args = TokenizeCommand(line);
        if (args.empty()) continue;

        // Like Quake: commands first, then aliases, then cvars
        const ConsoleCommand *command = FindCommand(args[0]);
        if (command != nullptr)
        {
            command->function(args);
            continue;
        }

        std::string lower = args[0];
        for (char &c : lower) c = (char)tolower((unsigned char)c);
        auto alias = aliases.find(lower);
        if (alias != aliases.end())
        {
            if (aliasDepth >= 16)
            {
                ConsolePrintf("Alias \"%s\" runs too many aliases inside each other, stopped", args[0].c_str());
                continue;
            }
            aliasDepth++;
            ExecuteCommandText(alias->second);
            aliasDepth--;
            continue;
        }

        if (!HandleCvar(args)) ConsolePrintf("Unknown command \"%s\"", args[0].c_str());
    }
}

bool ExecConfig(const char *path, bool quietIfMissing)
{
    // Checked first, so a missing optional file doesn't log a warning
    if (quietIfMissing && !GameDataFileExists(path)) return false;

    char *text = LoadFileText(path);
    if (text == nullptr)
    {
        ConsolePrintf("Couldn't exec %s", path);
        return false;
    }

    ConsolePrintf("Execing %s", path);
    bool wasRunningConfig = runningConfig;  // Configs can exec other configs
    runningConfig = true;
    ExecuteCommandText(text);
    runningConfig = wasRunningConfig;
    UnloadFileText(text);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Binds, aliases and configs

static void Cmd_Bind(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        ConsolePrintf("Usage: bind <key> \"<command>\"");
        return;
    }

    int key = KeyFromName(args[1]);
    if (key < 0)
    {
        ConsolePrintf("\"%s\" isn't a key name", args[1].c_str());
        return;
    }

    if (args.size() == 2)
    {
        auto bind = GetBinds().find(key);
        if (bind == GetBinds().end()) ConsolePrintf("\"%s\" isn't bound", KeyToName(key).c_str());
        else ConsolePrintf("\"%s\" = \"%s\"", KeyToName(key).c_str(), bind->second.c_str());
        return;
    }

    // Like Quake, everything after the key is the command, so bind x fly and bind x "fly" both work
    std::string command = args[2];
    for (size_t i = 3; i < args.size(); i++) command += " " + args[i];
    SetBind(key, command);
}

static void Cmd_Unbind(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        ConsolePrintf("Usage: unbind <key>");
        return;
    }

    int key = KeyFromName(args[1]);
    if (key < 0) ConsolePrintf("\"%s\" isn't a key name", args[1].c_str());
    else RemoveBind(key);
}

static void Cmd_UnbindAll(const std::vector<std::string> &args)
{
    (void)args;
    ClearBinds();
}

static void Cmd_Alias(const std::vector<std::string> &args)
{
    if (args.size() == 1)
    {
        if (aliases.empty()) ConsolePrintf("No aliases");
        for (const auto &alias : aliases) ConsolePrintf("%s : %s", alias.first.c_str(), alias.second.c_str());
        return;
    }

    std::string name = args[1];
    for (char &c : name) c = (char)tolower((unsigned char)c);

    if (args.size() == 2)
    {
        auto alias = aliases.find(name);
        if (alias == aliases.end()) ConsolePrintf("No alias named \"%s\"", args[1].c_str());
        else ConsolePrintf("%s : %s", alias->first.c_str(), alias->second.c_str());
        return;
    }

    std::string commands = args[2];
    for (size_t i = 3; i < args.size(); i++) commands += " " + args[i];
    aliases[name] = commands;
}

static void Cmd_Exec(const std::vector<std::string> &args)
{
    if (args.size() < 2) ConsolePrintf("Usage: exec <file>");
    else ExecConfig(args[1].c_str(), false);
}

// Writes a value so it reads back as one argument, like "+forward" or "say \"hi\""
static std::string Quoted(const std::string &text)
{
    return "\"" + text + "\"";
}

bool SaveConfig(const char *path)
{
    if (path == nullptr) path = CONFIG_FILE;

    // Like Quake's config.cfg: unbindall first, so binds removed since default.cfg stay removed
    std::string text = "// Written by the options menu and cfg_save. It's overwritten every time, so put your own commands and aliases in autoexec.cfg\n";
    text += "unbindall\n";
    for (const auto &bind : GetBinds()) text += "bind " + Quoted(KeyToName(bind.first)) + " " + Quoted(bind.second) + "\n";
    for (const Cvar *cvar : AllCvars()) text += std::string(cvar->name) + " " + Quoted(TextFormat("%g", cvar->value)) + "\n";

    return SaveFileText(path, text.c_str());
}

static void Cmd_CfgSave(const std::vector<std::string> &args)
{
    const char *path = (args.size() > 1) ? args[1].c_str() : CONFIG_FILE;
    if (SaveConfig(path)) ConsolePrintf("Saved %d binds and %d cvars to %s", (int)GetBinds().size(), (int)AllCvars().size(), path);
    else ConsolePrintf("Couldn't save %s", path);
}

// ---------------------------------------------------------------------------------------------
// Controls

static void Cmd_Toggle(const std::vector<std::string> &args)
{
    Cvar *cvar = (args.size() > 1) ? FindCvar(args[1]) : nullptr;
    if (cvar == nullptr)
    {
        ConsolePrintf("Usage: toggle <cvar>");
        return;
    }
    cvar->value = (cvar->value != 0.0f) ? 0.0f : 1.0f;
}

static void Cmd_ToggleCursor(const std::vector<std::string> &args)
{
    (void)args;
    if (IsCursorHidden()) EnableCursor();
    else DisableCursor();
}

// ---------------------------------------------------------------------------------------------

// From opengl32.lib, declared here because the GL headers need the parts of windows.h left out above
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
extern "C" __declspec(dllimport) const unsigned char *__stdcall glGetString(unsigned int name);

static std::string GetCpuName()
{
#if defined(_MSC_VER)
    int info[4] = { 0 };
    char brand[49] = { 0 };
    __cpuid(info, 0x80000000);
    if ((unsigned int)info[0] < 0x80000004) return "Unknown";

    for (int i = 0; i < 3; i++)
    {
        __cpuid(info, 0x80000002 + i);
        memcpy(brand + i*16, info, sizeof(info));
    }

    std::string name = brand;
    name.erase(0, name.find_first_not_of(' '));
    return name;
#else
    return "Unknown";
#endif
}

static double GetTotalRamGB()
{
#if defined(_WIN32)
    MEMORYSTATUSEX status = { sizeof(status) };
    if (GlobalMemoryStatusEx(&status)) return status.ullTotalPhys/(1024.0*1024.0*1024.0);
#endif
    return 0.0;
}

static void Cmd_About(const std::vector<std::string> &args)
{
    (void)args;
    int monitor = GetCurrentMonitor();

    ConsolePrintf("Program: %s", ConsoleAppName());
    ConsolePrintf("raylib: %s", RAYLIB_VERSION);
    ConsolePrintf("raygui: %s", RAYGUI_VERSION);
    ConsolePrintf("OpenGL: %s", (const char *)glGetString(GL_VERSION));
    ConsolePrintf("GPU: %s (%s)", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VENDOR));
    ConsolePrintf("CPU: %s, %u threads", GetCpuName().c_str(), std::thread::hardware_concurrency());
    ConsolePrintf("RAM: %.1f GB", GetTotalRamGB());
    ConsolePrintf("Display: %s, %dx%d @ %d Hz", GetMonitorName(monitor), GetMonitorWidth(monitor), GetMonitorHeight(monitor), GetMonitorRefreshRate(monitor));
    ConsolePrintf("Window: %dx%d", GetScreenWidth(), GetScreenHeight());
#if defined(_MSC_VER)
    ConsolePrintf("Compiler: MSVC %d", _MSC_VER);
#endif
}

static void Cmd_Clear(const std::vector<std::string> &args)
{
    (void)args;
    ConsoleClear();
}

static void Cmd_Help(const std::vector<std::string> &args)
{
    (void)args;
    ConsolePrintf("Commands:");
    for (const ConsoleCommand &command : commands) ConsolePrintf("  %s - %s", command.name, command.description);
    for (const ConsoleCommand &command : addedCommands)
    {
        if (command.name[0] != '-') ConsolePrintf("  %s - %s", command.name, command.description);  // - commands just undo their + one
    }
    ConsolePrintf("Cvars (type the name to see the value, or the name and a value to set it):");
    for (const Cvar *cvar : AllCvars()) ConsolePrintf("  %s - %s", cvar->name, cvar->description);
}

// "C:\...", "C:/...", "\\server\..." or "/..."
static bool IsPathAbsolute(const std::string &path)
{
    if (path.size() >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':') return true;
    return !path.empty() && (path[0] == '/' || path[0] == '\\');
}

static void Cmd_WriteLogs(const std::vector<std::string> &args)
{
    // Relative paths are relative to the game's folder, since main() changes into it at startup
    std::string path = (args.size() > 1) ? args[1] : "console.log";
    if (!IsPathAbsolute(path)) path = std::string(GetWorkingDirectory()) + "/" + path;

    int lineCount = ConsoleSaveLog(path.c_str());
    if (lineCount < 0) ConsolePrintf("Couldn't write logs to %s", path.c_str());
    else ConsolePrintf("Wrote %d lines to %s", lineCount, path.c_str());
}
