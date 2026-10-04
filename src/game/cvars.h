#pragma once

#include <string>
#include <vector>

// args[0] is the command name itself, like argv in C
typedef void (*CommandFunction)(const std::vector<std::string> &args);

struct ConsoleCommand
{
    const char *name;
    const char *description;
    CommandFunction function;
};

// A console variable, like Quake's cvars: a named setting that can be read or changed from the console
struct Cvar
{
    const char *name;
    const char *description;
    float value;
    float defaultValue;
};

extern Cvar r_showentities;
extern Cvar r_use_pointlight_shadows;
extern Cvar r_showcolliders;

// Runs console commands, like Quake's command buffer: commands are separated by ; or new lines,
// "quoted text" is one argument, and // starts a comment. Each command can be a command, an alias
// or a cvar. Used by the console, binds, aliases and .cfg files
void ExecuteCommandText(const std::string &text);

// Runs every command in a .cfg file, read through the game data. Returns false if it couldn't be read
bool ExecConfig(const char *path, bool quietIfMissing);

// Splits one command into arguments on whitespace, keeping "quoted text" together
std::vector<std::string> TokenizeCommand(const std::string &command);

// Looks up a command by name, ignoring case. Returns nullptr if there's no such command
const ConsoleCommand *FindCommand(const std::string &name);

// Looks up a cvar by name, ignoring case. Returns nullptr if there's no such cvar
Cvar *FindCvar(const std::string &name);

// If args[0] names a cvar, prints its value, or sets it when a value is given. Returns false if it's not a cvar
bool HandleCvar(const std::vector<std::string> &args);
