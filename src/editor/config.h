#pragma once

// editor.cfg: the editor's binds and settings, in the same format as the game's .cfg files.
// It lives next to editor.exe, and is saved automatically whenever a setting changes. Hand edits made
// while the editor is open are picked up when the file is saved (hotreload.cpp), so they aren't lost.
//
//   bind "ctrl+s" "save_level"    Binds can use ctrl+, shift+ and alt+ in front of a key
//   ed_darkmode "1"               Settings are set like the game's cvars

#include <string>
#include <vector>

struct EditorState;

// Camera movement, held by the +/- commands (+forward, -forward...) while their keys are down
struct CameraButtons
{
    bool forward;
    bool back;
    bool moveLeft;
    bool moveRight;
    bool moveUp;
    bool moveDown;
    bool speed;  // Faster
};

// Saved in editor.cfg as ed_ settings, and changed in Edit > Preferences
struct EditorSettings
{
    // General
    int undoSteps;           // How many changes undo remembers

    // View
    float fieldOfView;       // Camera, in degrees
    float moveSpeed;         // Camera speed, units per second
    bool showGrid;
    bool showLabels;         // Entity names over their markers
    float gridSize;          // Grid spacing and snapping step, in world units. [ and ] step through GRID_SIZES in editor.cpp
    bool snapToGrid;         // Dragged and created entities land on the grid

    // Input
    float lookSensitivity;   // Radians per pixel of mouse movement
    bool invertLook;         // Moving the mouse up looks down
};

extern CameraButtons cameraButtons;
extern EditorSettings editorSettings;

// Sets up the default binds and settings, then runs editor.cfg if it exists. Call after LoadEditorStyle
void LoadEditorConfig(EditorState &editor);
void SaveEditorConfig();
void SaveEditorConfigIfChanged();  // On exit: only a change still waiting to be saved, so hand edits aren't overwritten
std::string GetEditorConfigPath();

// Runs editor.cfg again if it was edited by hand since the editor last wrote or read it. Returns true if
// it did; the editor's own saves don't count
bool ReloadEditorConfigIfEdited(EditorState &editor);

// Runs the commands of keys pressed or released this frame
void UpdateEditorBinds(EditorState &editor);

// Runs the - command of every key still held, so camera movement doesn't stay stuck on
void ReleaseEditorBinds();

// Switches dark mode and saves editor.cfg, so it's remembered next time
void ToggleDarkMode();

// Call after changing a setting. editor.cfg gets saved once the mouse button is up, so dragging a
// slider writes the file once rather than every frame
void MarkSettingsChanged();
void UpdateConfigSaving();  // Call once a frame

// For the Input tab of Preferences: every command that can be bound, with a label for it
struct BindableCommand
{
    const char *name;
    const char *label;
};
std::vector<BindableCommand> GetBindableCommands();
std::string GetCommandBinds(const char *command);              // Its keys, like "ctrl+y, ctrl+shift+z", or "" if none
void BindCommand(const char *command, const std::string &combo);  // Replaces the command's keys with this one
void UnbindCommand(const char *command);
void ResetBindsToDefaults();

// Turns a key just pressed into a combo name like "ctrl+s", for rebinding. Call every frame while
// waiting for a key; returns true once one is pressed. A modifier pressed and let go on its own
// counts as itself, like "shift"
bool CaptureKeyCombo(std::string &combo);
