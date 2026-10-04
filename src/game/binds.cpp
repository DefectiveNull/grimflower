#include "binds.h"
#include "cvars.h"
#include "keys.h"

InputButtons inputButtons = {};

static std::map<int, std::string> binds;

// Keys whose + command ran, and the - command to run when they're released. Saved at press time,
// so rebinding a held key still releases what it pressed
static std::map<int, std::string> held;

bool SetInputButton(const std::string &name, bool down)
{
    if (name == "forward") inputButtons.forward = down;
    else if (name == "back") inputButtons.back = down;
    else if (name == "moveleft") inputButtons.moveLeft = down;
    else if (name == "moveright") inputButtons.moveRight = down;
    else if (name == "jump") inputButtons.jump = down;
    else if (name == "moveup") inputButtons.moveUp = down;
    else if (name == "movedown") inputButtons.moveDown = down;
    else return false;
    return true;
}

void SetBind(int key, const std::string &command)
{
    binds[key] = command;
}

void RemoveBind(int key)
{
    binds.erase(key);
}

void ClearBinds()
{
    binds.clear();
}

const std::map<int, std::string> &GetBinds()
{
    return binds;
}

static void Release(int key)
{
    auto it = held.find(key);
    if (it == held.end()) return;
    std::string command = it->second;
    held.erase(it);
    ExecuteCommandText(command);
}

void UpdateBinds()
{
    // Copied, since a bind can run "bind" or "unbind" and change the map while we go through it
    std::map<int, std::string> current = binds;

    for (const auto &bind : current)
    {
        int key = bind.first;
        const std::string &command = bind.second;

        if (IsInputPressed(key) && held.find(key) == held.end())
        {
            ExecuteCommandText(command);
            if (!command.empty() && command[0] == '+') held[key] = "-" + command.substr(1);
        }
        if (IsInputReleased(key)) Release(key);
    }

    // Keys released after their bind was removed
    for (auto it = held.begin(); it != held.end();)
    {
        int key = (it++)->first;
        if (current.find(key) == current.end() && IsInputReleased(key)) Release(key);
    }
}

void ReleaseBinds()
{
    while (!held.empty()) Release(held.begin()->first);
}
