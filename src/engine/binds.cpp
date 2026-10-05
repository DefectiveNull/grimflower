#include "binds.h"
#include "cvars.h"
#include "keys.h"

static std::map<int, std::string> binds;

// Keys whose + command ran, and the - command to run when they're released. Saved at press time,
// so rebinding a held key still releases what it pressed
static std::map<int, std::string> held;

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
