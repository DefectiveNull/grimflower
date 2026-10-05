#include "config.h"
#include "editor.h"
#include "keys.h"
#include "style.h"
#include "raylib.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

CameraButtons cameraButtons = {};
EditorSettings editorSettings = { 200, 70.0f, 8.0f, true, true, 0.25f, true, 0.003f, false };

static const char *CONFIG_FILE = "editor.cfg";

// Used when there's no editor.cfg yet, and as the starting point before it runs
static const char *DEFAULT_CONFIG = R"(
unbindall

// Files
bind "ctrl+n" "new_level"
bind "ctrl+o" "open_level"
bind "ctrl+s" "save_level"
bind "ctrl+shift+s" "save_level_as"

// Undo
bind "ctrl+z" "undo"
bind "ctrl+y" "redo"
bind "ctrl+shift+z" "redo"

// Entities
bind "del" "delete_entity"
bind "alt+q" "rotate_left"
bind "alt+r" "rotate_right"

// Grid
bind "[" "grid_smaller"
bind "]" "grid_bigger"

// Camera, while holding the right mouse button
bind "w" "+forward"
bind "s" "+back"
bind "a" "+moveleft"
bind "d" "+moveright"
bind "e" "+moveup"
bind "q" "+movedown"
bind "shift" "+speed"
)";

// The editor the commands act on. Set whenever binds or configs run
static EditorState *current = nullptr;

// ---------------------------------------------------------------------------------------------
// Key combos: a key code, plus flags for the modifiers that have to be held with it

static const int MOD_CTRL = 1 << 16;
static const int MOD_SHIFT = 1 << 17;
static const int MOD_ALT = 1 << 18;
static const int KEY_MASK = 0xFFFF;

static std::string Lowercase(std::string text)
{
    for (char &c : text) c = (char)tolower((unsigned char)c);
    return text;
}

// "ctrl+shift+s" to the S key with the Ctrl and Shift flags. -1 if it isn't a key
static int ComboFromName(const std::string &name)
{
    std::string rest = Lowercase(name);
    int modifiers = 0;

    struct Prefix { const char *text; int flag; };
    static const Prefix prefixes[] = { { "ctrl+", MOD_CTRL }, { "shift+", MOD_SHIFT }, { "alt+", MOD_ALT } };

    bool found = true;
    while (found)
    {
        found = false;
        for (const Prefix &prefix : prefixes)
        {
            size_t length = strlen(prefix.text);
            if (rest.size() > length && rest.compare(0, length, prefix.text) == 0)
            {
                modifiers |= prefix.flag;
                rest = rest.substr(length);
                found = true;
            }
        }
    }

    int key = KeyFromName(rest);
    return (key < 0) ? -1 : (key | modifiers);
}

static std::string ComboToName(int combo)
{
    std::string name;
    if (combo & MOD_CTRL) name += "ctrl+";
    if (combo & MOD_SHIFT) name += "shift+";
    if (combo & MOD_ALT) name += "alt+";
    return name + KeyToName(combo & KEY_MASK);
}

// The modifier flag a key is itself, so holding Shift doesn't count as "shift+shift"
static int OwnModifier(int key)
{
    switch (key)
    {
        case KEY_LEFT_CONTROL: return MOD_CTRL;
        case KEY_LEFT_SHIFT: return MOD_SHIFT;
        case KEY_LEFT_ALT: return MOD_ALT;
        default: return 0;
    }
}

static int HeldModifiers()
{
    int modifiers = 0;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) modifiers |= MOD_CTRL;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) modifiers |= MOD_SHIFT;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) modifiers |= MOD_ALT;
    return modifiers;
}

static std::map<int, std::string> binds;  // Combo to command
static std::map<int, std::string> held;   // Physical key to the - command to run when it's released

// ---------------------------------------------------------------------------------------------
// Running commands. Same syntax as the game's console: ; or new lines between commands,
// "quoted text" as one argument, and // comments

static std::vector<std::string> Tokenize(const std::string &command)
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
            i++;
        }
        else
        {
            while (i < command.size() && !isspace((unsigned char)command[i])) arg += command[i++];
        }
        args.push_back(arg);
    }
    return args;
}

static std::vector<std::string> SplitCommands(const std::string &text)
{
    std::vector<std::string> commands;
    std::string commandText;
    bool quoted = false;

    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];
        if (c == '"') quoted = !quoted;

        if (!quoted && c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n') i++;
            c = '\n';
        }

        if (!quoted && (c == ';' || c == '\n' || c == '\r'))
        {
            commands.push_back(commandText);
            commandText.clear();
            if (c == '\n') quoted = false;
        }
        else
        {
            commandText += c;
        }
    }
    commands.push_back(commandText);
    return commands;
}

static void Execute(const std::string &text);

// ---------------------------------------------------------------------------------------------
// Commands

typedef std::vector<std::string> Args;

static void Cmd_Bind(const Args &args)
{
    if (args.size() < 3) { TraceLog(LOG_WARNING, "CONFIG: Usage: bind <key> \"<command>\""); return; }

    int combo = ComboFromName(args[1]);
    if (combo < 0) { TraceLog(LOG_WARNING, "CONFIG: \"%s\" isn't a key name", args[1].c_str()); return; }

    std::string command = args[2];
    for (size_t i = 3; i < args.size(); i++) command += " " + args[i];
    binds[combo] = command;
}

static void Cmd_Unbind(const Args &args)
{
    int combo = (args.size() > 1) ? ComboFromName(args[1]) : -1;
    if (combo < 0) TraceLog(LOG_WARNING, "CONFIG: Usage: unbind <key>");
    else binds.erase(combo);
}

static void Cmd_UnbindAll(const Args &) { binds.clear(); }

static void Cmd_NewLevel(const Args &) { RequestAction(*current, ACTION_NEW_LEVEL); }
static void Cmd_OpenLevel(const Args &) { RequestAction(*current, ACTION_OPEN_LEVEL); }
static void Cmd_SaveLevel(const Args &) { SaveCurrentLevel(*current, false); }
static void Cmd_SaveLevelAs(const Args &) { SaveCurrentLevel(*current, true); }
static void Cmd_ExitEditor(const Args &) { RequestAction(*current, ACTION_EXIT); }
static void Cmd_DeleteEntity(const Args &) { DeleteSelectedEntity(*current); }
static void Cmd_Deselect(const Args &) { current->selected = -1; }
static void Cmd_CreateEntity(const Args &) { OpenDialog(*current, DIALOG_CREATE_ENTITY); }
static void Cmd_CreateWorld(const Args &) { OpenDialog(*current, DIALOG_CREATE_WORLD); }
static void Cmd_WorldSettings(const Args &) { OpenDialog(*current, DIALOG_WORLD_SETTINGS); }
static void Cmd_EditorInfo(const Args &) { OpenDialog(*current, DIALOG_EDITOR_INFO); }
static void Cmd_ToggleDarkMode(const Args &) { ToggleDarkMode(); }
static void Cmd_RotateLeft(const Args &) { RotateSelectedEntity(*current, 45.0f); }
static void Cmd_RotateRight(const Args &) { RotateSelectedEntity(*current, -45.0f); }
static void Cmd_ReloadWorld(const Args &) { ReloadWorld(*current); }
static void Cmd_GridSmaller(const Args &) { ChangeGridSize(*current, -1); }
static void Cmd_GridBigger(const Args &) { ChangeGridSize(*current, 1); }
static void Cmd_Undo(const Args &) { Undo(*current); }
static void Cmd_Redo(const Args &) { Redo(*current); }
static void Cmd_UndoHistory(const Args &) { OpenDialog(*current, DIALOG_UNDO_HISTORY); }

static void Cmd_Button(const Args &args)
{
    bool down = (args[0][0] == '+');
    std::string name = args[0].substr(1);
    if (name == "forward") cameraButtons.forward = down;
    else if (name == "back") cameraButtons.back = down;
    else if (name == "moveleft") cameraButtons.moveLeft = down;
    else if (name == "moveright") cameraButtons.moveRight = down;
    else if (name == "moveup") cameraButtons.moveUp = down;
    else if (name == "movedown") cameraButtons.moveDown = down;
    else if (name == "speed") cameraButtons.speed = down;
}

struct Command
{
    const char *name;
    void (*function)(const Args &args);
    const char *label;  // Shown in Preferences > Input. nullptr for commands that aren't for binding keys to
};

static const Command commands[] =
{
    { "bind", Cmd_Bind, nullptr }, { "unbind", Cmd_Unbind, nullptr }, { "unbindall", Cmd_UnbindAll, nullptr },

    { "new_level", Cmd_NewLevel, "New level" },
    { "open_level", Cmd_OpenLevel, "Open level" },
    { "save_level", Cmd_SaveLevel, "Save level" },
    { "save_level_as", Cmd_SaveLevelAs, "Save level as" },
    { "exit_editor", Cmd_ExitEditor, "Exit editor" },
    { "undo", Cmd_Undo, "Undo" },
    { "redo", Cmd_Redo, "Redo" },
    { "undo_history", Cmd_UndoHistory, "Undo history" },
    { "create_entity", Cmd_CreateEntity, "Create entity" },
    { "delete_entity", Cmd_DeleteEntity, "Delete entity" },
    { "deselect", Cmd_Deselect, "Deselect" },
    { "rotate_left", Cmd_RotateLeft, "Rotate left 45 degrees" },
    { "rotate_right", Cmd_RotateRight, "Rotate right 45 degrees" },
    { "grid_smaller", Cmd_GridSmaller, "Smaller grid" },
    { "grid_bigger", Cmd_GridBigger, "Bigger grid" },
    { "create_world", Cmd_CreateWorld, "Create world" },
    { "reload_world", Cmd_ReloadWorld, "Reload world model" },
    { "world_settings", Cmd_WorldSettings, "World settings" },
    { "toggle_darkmode", Cmd_ToggleDarkMode, "Toggle dark mode" },
    { "editor_info", Cmd_EditorInfo, "Editor info" },

    { "+forward", Cmd_Button, "Camera forward" }, { "-forward", Cmd_Button, nullptr },
    { "+back", Cmd_Button, "Camera back" }, { "-back", Cmd_Button, nullptr },
    { "+moveleft", Cmd_Button, "Camera left" }, { "-moveleft", Cmd_Button, nullptr },
    { "+moveright", Cmd_Button, "Camera right" }, { "-moveright", Cmd_Button, nullptr },
    { "+moveup", Cmd_Button, "Camera up" }, { "-moveup", Cmd_Button, nullptr },
    { "+movedown", Cmd_Button, "Camera down" }, { "-movedown", Cmd_Button, nullptr },
    { "+speed", Cmd_Button, "Camera faster (hold)" }, { "-speed", Cmd_Button, nullptr },
};

// Settings, set like cvars: ed_movespeed 8. Each points at one field of editorSettings
struct Setting
{
    const char *name;
    float *number;
    bool *flag;     // Saved as 0 or 1
    int *integer;
};

static const Setting settings[] =
{
    { "ed_undosteps", nullptr, nullptr, &editorSettings.undoSteps },
    { "ed_fov", &editorSettings.fieldOfView, nullptr, nullptr },
    { "ed_movespeed", &editorSettings.moveSpeed, nullptr, nullptr },
    { "ed_showgrid", nullptr, &editorSettings.showGrid, nullptr },
    { "ed_showlabels", nullptr, &editorSettings.showLabels, nullptr },
    { "ed_gridsize", &editorSettings.gridSize, nullptr, nullptr },
    { "ed_snap", nullptr, &editorSettings.snapToGrid, nullptr },
    { "ed_sensitivity", &editorSettings.lookSensitivity, nullptr, nullptr },
    { "ed_invertlook", nullptr, &editorSettings.invertLook, nullptr },
};

static void Execute(const std::string &text)
{
    for (const std::string &line : SplitCommands(text))
    {
        Args args = Tokenize(line);
        if (args.empty()) continue;
        std::string name = Lowercase(args[0]);

        bool handled = false;
        for (const Command &command : commands)
        {
            if (name == command.name) { command.function(args); handled = true; break; }
        }
        if (handled) continue;

        // Dark mode is kept by the style, the other settings here
        if (name == "ed_darkmode")
        {
            if (args.size() > 1) SetDarkMode(atof(args[1].c_str()) != 0.0);
            continue;
        }
        for (const Setting &setting : settings)
        {
            if (name == setting.name)
            {
                if (args.size() > 1)
                {
                    double value = atof(args[1].c_str());
                    if (setting.number) *setting.number = (float)value;
                    if (setting.flag) *setting.flag = (value != 0.0);
                    if (setting.integer) *setting.integer = (int)value;
                }
                handled = true;
                break;
            }
        }
        if (!handled) TraceLog(LOG_WARNING, "CONFIG: Unknown command \"%s\"", args[0].c_str());
    }
}

// ---------------------------------------------------------------------------------------------

std::string GetEditorConfigPath()
{
    return std::string(GetApplicationDirectory()) + CONFIG_FILE;
}

static bool configNeedsSaving = false;

// editor.cfg as the editor last wrote or read it. When the file changes on disk, matching this means it
// was the editor's own save, anything else is a hand edit
static std::string lastConfigText;

// The default binds, then editor.cfg on top of them. Returns false if editor.cfg couldn't be read
static bool RunConfigFile()
{
    Execute(DEFAULT_CONFIG);
    std::map<int, std::string> defaults = binds;

    std::string path = GetEditorConfigPath();
    char *text = LoadFileText(path.c_str());
    if (text == nullptr) return false;
    lastConfigText = text;
    Execute(text);
    UnloadFileText(text);
    TraceLog(LOG_INFO, "CONFIG: Ran %s", path.c_str());

    // editor.cfg starts with unbindall, so commands added to the editor after it was saved would have no
    // keys. Give them their default ones, as long as the command isn't bound already and the key is free
    for (const auto &bind : defaults)
    {
        if (binds.count(bind.first)) continue;

        bool commandBound = false;
        for (const auto &existing : binds) if (existing.second == bind.second) commandBound = true;
        if (commandBound) continue;

        binds[bind.first] = bind.second;
        configNeedsSaving = true;  // So the file gets them too
        TraceLog(LOG_INFO, "CONFIG: Added the default bind %s for %s", ComboToName(bind.first).c_str(), bind.second.c_str());
    }
    return true;
}

void LoadEditorConfig(EditorState &editor)
{
    current = &editor;
    if (!FileExists(GetEditorConfigPath().c_str()))
    {
        Execute(DEFAULT_CONFIG);
        TraceLog(LOG_INFO, "CONFIG: No %s yet, using the default binds and settings", CONFIG_FILE);
        return;
    }
    RunConfigFile();
}

bool ReloadEditorConfigIfEdited(EditorState &editor)
{
    char *text = LoadFileText(GetEditorConfigPath().c_str());
    if (text == nullptr) return false;
    bool ownSave = (lastConfigText == text);
    UnloadFileText(text);
    if (ownSave) return false;

    // Let go of held keys first, so a camera button whose key moved doesn't stay stuck on
    current = &editor;
    ReleaseEditorBinds();
    return RunConfigFile();
}

void SaveEditorConfig()
{
    std::string text = "// Written by the editor whenever a setting changes. It can be edited by hand, even with the editor open: it picks up the changes when the file is saved\n";
    text += "unbindall\n";
    for (const auto &bind : binds) text += "bind \"" + ComboToName(bind.first) + "\" \"" + bind.second + "\"\n";

    text += TextFormat("ed_darkmode \"%d\"\n", IsDarkMode() ? 1 : 0);
    for (const Setting &setting : settings)
    {
        if (setting.number) text += TextFormat("%s \"%g\"\n", setting.name, *setting.number);
        if (setting.flag) text += TextFormat("%s \"%d\"\n", setting.name, *setting.flag ? 1 : 0);
        if (setting.integer) text += TextFormat("%s \"%d\"\n", setting.name, *setting.integer);
    }

    std::string path = GetEditorConfigPath();
    if (SaveFileText(path.c_str(), text.c_str())) lastConfigText = text;
    else TraceLog(LOG_WARNING, "CONFIG: Couldn't save %s", path.c_str());
    configNeedsSaving = false;
}

void ToggleDarkMode()
{
    SetDarkMode(!IsDarkMode());
    SaveEditorConfig();
}

static void Release(int key)
{
    auto it = held.find(key);
    if (it == held.end()) return;
    std::string command = it->second;
    held.erase(it);
    Execute(command);
}

void UpdateEditorBinds(EditorState &editor)
{
    current = &editor;
    int modifiers = HeldModifiers();

    // Each physical key with any bind, with or without modifiers
    std::set<int> keys;
    for (const auto &bind : binds)
    {
        keys.insert(bind.first & KEY_MASK);
    }

    for (int key : keys)
    {
        if (IsInputPressed(key) && held.find(key) == held.end())
        {
            // The bind for exactly the modifiers held, like ctrl+s, wins over the plain key's bind
            auto bind = binds.find(key | (modifiers & ~OwnModifier(key)));
            if (bind == binds.end()) bind = binds.find(key);

            if (bind != binds.end())
            {
                std::string command = bind->second;  // Copied, running it can change the binds
                Execute(command);
                if (!command.empty() && command[0] == '+') held[key] = "-" + command.substr(1);
            }
        }
        if (IsInputReleased(key)) Release(key);
    }
}

void ReleaseEditorBinds()
{
    while (!held.empty()) Release(held.begin()->first);
}

// ---------------------------------------------------------------------------------------------
// Preferences

void MarkSettingsChanged()
{
    configNeedsSaving = true;
}

void UpdateConfigSaving()
{
    if (!configNeedsSaving || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;  // Wait until a slider drag is over
    SaveEditorConfig();
}

void SaveEditorConfigIfChanged()
{
    if (configNeedsSaving) SaveEditorConfig();
}

std::vector<BindableCommand> GetBindableCommands()
{
    std::vector<BindableCommand> list;
    for (const Command &command : commands)
    {
        if (command.label) list.push_back({ command.name, command.label });
    }
    return list;
}

std::string GetCommandBinds(const char *command)
{
    std::string keys;
    for (const auto &bind : binds)
    {
        if (bind.second != command) continue;
        if (!keys.empty()) keys += ", ";
        keys += ComboToName(bind.first);
    }
    return keys;
}

void UnbindCommand(const char *command)
{
    for (auto it = binds.begin(); it != binds.end();)
    {
        if (it->second == command) it = binds.erase(it);
        else ++it;
    }
}

void BindCommand(const char *command, const std::string &comboName)
{
    int combo = ComboFromName(comboName);
    if (combo < 0) return;

    // One key per command from here, and like Quake, binding a key takes it from whatever had it
    UnbindCommand(command);
    binds[combo] = command;
    SaveEditorConfig();
}

void ResetBindsToDefaults()
{
    Execute(DEFAULT_CONFIG);  // Only binds, starting with unbindall
    SaveEditorConfig();
}

bool CaptureKeyCombo(std::string &combo)
{
    // A modifier on its own counts once it's let go without another key being pressed with it
    static int lonelyModifier = 0;

    int key = GetKeyPressed();
    while (key != 0)
    {
        int modifier = OwnModifier(key == KEY_RIGHT_CONTROL ? KEY_LEFT_CONTROL : key == KEY_RIGHT_SHIFT ? KEY_LEFT_SHIFT : key == KEY_RIGHT_ALT ? KEY_LEFT_ALT : key);
        if (modifier != 0)
        {
            lonelyModifier = (modifier == MOD_CTRL) ? KEY_LEFT_CONTROL : (modifier == MOD_SHIFT) ? KEY_LEFT_SHIFT : KEY_LEFT_ALT;
        }
        else if (KeyToName(key) != "unknown")
        {
            lonelyModifier = 0;
            combo = ComboToName(key | HeldModifiers());
            return true;
        }
        key = GetKeyPressed();
    }

    if (lonelyModifier != 0 && IsInputReleased(lonelyModifier))
    {
        combo = KeyToName(lonelyModifier);
        lonelyModifier = 0;
        return true;
    }
    return false;
}
