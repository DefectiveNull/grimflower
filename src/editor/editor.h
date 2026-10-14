#pragma once

// Shared state for the level editor, and the functions each part of it provides

#include "raylib.h"
#include "level.h"
#include "world.h"
#include "skybox.h"

#include <string>
#include <vector>

#define EDITOR_NAME "Grimflower Editor"
#define EDITOR_VERSION "0.1"

// Screen layout
static const int MENU_BAR_HEIGHT = 26;
static const int STATUS_BAR_HEIGHT = 24;
static const int DETAILS_PANEL_WIDTH = 340;

// A copy of the level from before a change, so the change can be undone
struct UndoStep
{
    std::string description;  // The change made after this copy, like "Move light"
    Level level;
    int selected;
    int stateId;              // Which version of the level this copy is
};

enum MenuId
{
    MENU_NONE,
    MENU_FILE,
    MENU_EDIT,
    MENU_VIEW,
    MENU_ABOUT
};

enum DialogId
{
    DIALOG_NONE,
    DIALOG_CREATE_WORLD,
    DIALOG_WORLD_SETTINGS,
    DIALOG_PREFERENCES,
    DIALOG_EDITOR_INFO,
    DIALOG_UNSAVED_CHANGES,  // Asks before an action would throw away unsaved changes
    DIALOG_UNDO_HISTORY,
    DIALOG_COLOR_PICKER
};

// Actions that throw away the current level, so they ask first if it has unsaved changes
enum PendingAction
{
    ACTION_NONE,
    ACTION_NEW_LEVEL,
    ACTION_OPEN_LEVEL,
    ACTION_EXIT
};

struct EditorState
{
    std::string dataRoot;     // Project folder with maps/, model/ and textures/. Also the working directory

    // The level being edited
    Level level;
    std::string levelPath;    // File it was opened from or saved to. Empty until a new level is saved
    bool unsavedChanges;      // The current version isn't the one last saved

    // Undo history (undo.cpp). Every version of the level gets an id, so undoing back to the saved
    // version can tell there's nothing unsaved anymore
    std::vector<UndoStep> undoSteps;   // Oldest first
    std::vector<UndoStep> redoSteps;   // Next redo last
    int stateId;                       // Version of the level now
    int savedStateId;                  // Version last opened or saved
    int nextStateId;

    // Its world model, drawn with the game's lighting
    World world;
    bool worldLoaded;
    std::string loadedModel;  // Model and scale the world was loaded from, to tell when undo changes them
    float loadedScale;
    Sun defaultSun;           // Sun the world gets when the level has no "sun" entity
    Skybox skybox;            // The level's info.skybox
    std::string skyboxTried;  // Path last loaded (or tried), so a broken one isn't retried every frame

    // Fly camera: hold the right mouse button to look around, WASD/Q/E to move
    Camera camera;
    float yaw;
    float pitch;
    bool looking;

    int selected;             // Index into level.entities, -1 for none

    // Dragging the selected entity with the left mouse button: horizontally, or vertically while holding Alt
    bool dragging;            // Left button went down on an entity and hasn't been released
    bool dragMoved;           // The mouse moved far enough to count as a drag rather than a click
    bool dragVertical;        // Which way the current drag plane goes
    Vector2 dragStartMouse;
    Vector3 dragPlanePoint;   // The entity moves where the mouse ray meets this plane...
    Vector3 dragPlaneNormal;
    Vector3 dragOffset;       // ...plus this, so it doesn't jump to the cursor when the drag starts

    // Or resizing a volume by dragging one of the handles in the middle of its faces. The face slides along
    // its own axis only, landing on the grid
    int resizeFace;           // -1 when moving instead. 0-5: -x, +x, -y, +y, -z, +z
    float resizeOffset;       // The face's position minus where along the axis it was grabbed

    // "Point At": the next click on the level aims an entity's direction parameter at the clicked spot
    bool pointing;
    int pointEntity;
    std::string pointParameter;

    MenuId openMenu;
    DialogId dialog;
    PendingAction pendingAction;
    bool textEditing;         // A text field has the keyboard, so shortcuts are off
    bool quit;

    std::string status;       // Message in the status bar, like "Saved maps/dev.json"
    double statusTime;
};

// editor.cpp: working with the level
void InitEditor(EditorState &editor, const std::string &dataRoot);
void ShutdownEditor(EditorState &editor);
void SetStatus(EditorState &editor, const char *format, ...);
std::string ToDataPath(const EditorState &editor, const std::string &path);  // Absolute path to one relative to the data root, if it's inside it
void RequestAction(EditorState &editor, PendingAction action);              // Asks first if there are unsaved changes
void RunAction(EditorState &editor, PendingAction action);
bool SaveCurrentLevel(EditorState &editor, bool askForPath);               // askForPath: Save As
bool CreateWorld(EditorState &editor, const std::string &model, float scale);
void CreateEntity(EditorState &editor, const char *classname);
void ShowCreateEntityList(EditorState &editor);                 // Deselects, so the details panel shows the list of entity types
void DeleteSelectedEntity(EditorState &editor);
void RotateSelectedEntity(EditorState &editor, float degrees);  // Positive turns left, seen from above
bool ReloadWorld(EditorState &editor);                           // Reads the world model's .obj from disk again
void ChangeGridSize(EditorState &editor, int steps);             // Steps through the grid sizes: -1 smaller, 1 bigger
float SnapToGrid(float value);                                   // The nearest grid line, or value as is with snapping off
void StartPointAt(EditorState &editor, int entityIndex, const std::string &parameter);
void CancelPointAt(EditorState &editor);
bool RaycastWorld(const EditorState &editor, Ray ray, Vector3 &point, Vector3 *normal);  // Where a ray first hits the level
void FrameCameraOnWorld(EditorState &editor);
void UpdateCameraFromAngles(EditorState &editor);

// undo.cpp: undo and redo
void RecordUndo(EditorState &editor, const std::string &description);  // Call just before changing the level
void ResetUndoHistory(EditorState &editor);                           // After opening or starting a level
void MarkSaved(EditorState &editor);
bool CanUndo(const EditorState &editor);
bool CanRedo(const EditorState &editor);
void Undo(EditorState &editor);
void Redo(EditorState &editor);
int GetHistoryPosition(const EditorState &editor);                    // 0 is the level as opened
void JumpToHistory(EditorState &editor, int position);                 // Undoes or redoes until there

// Loads or unloads the world model if the level's no longer matches the loaded one, like after an undo
void SyncWorldWithLevel(EditorState &editor);

// viewport.cpp: the 3D view
Rectangle GetViewportRect();
bool IsMouseOverViewport(const EditorState &editor);
void UpdateViewport(EditorState &editor);   // Camera movement and clicking entities
void DrawViewport(EditorState &editor);     // World, grid and entity markers. Call between BeginDrawing/EndDrawing
void UnloadViewport();

// menubar.cpp
void DrawMenuBar(EditorState &editor);
void DrawMenuDropdown(EditorState &editor);  // Separate so it can be drawn over everything else

// dialogs.cpp
void OpenDialog(EditorState &editor, DialogId dialog);
void OpenColorPicker(EditorState &editor, int entityIndex, const std::string &parameter);  // Color picker for an entity's color parameter
void DrawDialog(EditorState &editor);

// details.cpp
void DrawDetailsPanel(EditorState &editor);

// materialspanel.cpp: the details panel's Materials tab, in the given area
void DrawMaterialsPanel(EditorState &editor, Rectangle area);
