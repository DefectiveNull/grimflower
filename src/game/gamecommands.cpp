#include "gamecommands.h"
#include "console.h"
#include "cvars.h"
#include "game.h"
#include "gamedata.h"
#include "raylib.h"

#include <string>
#include <vector>

InputButtons inputButtons = {};

// ---------------------------------------------------------------------------------------------
// Controls

// +forward holds the forward button down, -forward lets go of it
static void Cmd_Button(const std::vector<std::string> &args)
{
    bool down = (args[0][0] == '+');
    std::string name = args[0].substr(1);
    if (name == "forward") inputButtons.forward = down;
    else if (name == "back") inputButtons.back = down;
    else if (name == "moveleft") inputButtons.moveLeft = down;
    else if (name == "moveright") inputButtons.moveRight = down;
    else if (name == "jump") inputButtons.jump = down;
    else if (name == "moveup") inputButtons.moveUp = down;
    else if (name == "movedown") inputButtons.moveDown = down;
    else if (name == "duck") inputButtons.duck = down;
}

static void Cmd_Fly(const std::vector<std::string> &args)
{
    (void)args;
    inputButtons.toggleFly = true;
}

// ---------------------------------------------------------------------------------------------
// Levels

std::string MapPath(const std::string &name)
{
    std::string path = name;
    for (char &c : path) if (c == '\\') c = '/';
    if (!IsFileExtension(path.c_str(), ".json")) path += ".json";
    if (path.find('/') == std::string::npos) path = "maps/" + path;
    return path;
}

static void Cmd_Map(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        ConsolePrintf("Usage: map <name>, like map dev to load maps/dev.json");
        return;
    }

    std::string path = MapPath(args[1]);
    if (!GameDataFileExists(path.c_str()))
    {
        ConsolePrintf("There's no level %s", path.c_str());
        return;
    }
    RequestMap(path);
}

// ---------------------------------------------------------------------------------------------

static const ConsoleCommand GAME_COMMANDS[] =
{
    { "fly", "Toggles fly mode", Cmd_Fly },
    { "map", "Loads a level: map <name>, like map dev for maps/dev.json", Cmd_Map },

    // Movement buttons: bind a key to the + one, and it's held while the key is
    { "+forward", "Move forward", Cmd_Button }, { "-forward", "", Cmd_Button },
    { "+back", "Move back", Cmd_Button }, { "-back", "", Cmd_Button },
    { "+moveleft", "Strafe left", Cmd_Button }, { "-moveleft", "", Cmd_Button },
    { "+moveright", "Strafe right", Cmd_Button }, { "-moveright", "", Cmd_Button },
    { "+jump", "Jump, or fly up", Cmd_Button }, { "-jump", "", Cmd_Button },
    { "+moveup", "Fly up", Cmd_Button }, { "-moveup", "", Cmd_Button },
    { "+movedown", "Fly down", Cmd_Button }, { "-movedown", "", Cmd_Button },
    { "+duck", "Crouch, or swim and fly down", Cmd_Button }, { "-duck", "", Cmd_Button },
};

void AddGameCommands()
{
    AddCommands(GAME_COMMANDS, (int)(sizeof(GAME_COMMANDS)/sizeof(GAME_COMMANDS[0])));
}
