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

// Object mode edits entities; Modelling mode edits the level's geometry, and only it can; Texturing mode puts
// textures on its faces and lines them up. Tab goes through them in this order
enum EditorMode
{
    MODE_OBJECT,
    MODE_MODELLING,
    MODE_TEXTURING,
    MODE_COUNT
};

// Modelling mode's tools (modellingmode.cpp). Select also moves, duplicates and resizes faces, with the mouse
// and modifier keys
enum EditTool
{
    TOOL_SELECT,
    TOOL_CUBE,       // B, with no edges selected
    TOOL_ROTATE,     // R
    TOOL_CUT,        // C
    TOOL_EXTRUDE,    // Ctrl+Shift
    TOOL_PUSHPULL,   // Alt+Shift
    TOOL_EDGE,       // E
    TOOL_BEVEL,      // B, with edges selected
    TOOL_COUNT
};

// How the 3D view draws the level (the menu behind the three dots in its corner)
enum ViewMode
{
    VIEW_LIT,        // Full 3D: lights, shadows, bounce, everything the game has
    VIEW_UNLIT,      // Shaded unlit: textures only
    VIEW_WIREFRAME
};

// Something in the level's geometry, under the mouse or clicked: a face of a mesh, and where on it (world space)
struct GeoPick
{
    int mesh = -1;
    int face = -1;
    Vector3 point = { 0.0f, 0.0f, 0.0f };
    Vector3 normal = { 0.0f, 1.0f, 0.0f };
};

// An edge of a mesh's shape, by its two vertices
struct GeoEdge
{
    int mesh;
    int a, b;
};

// What Modelling mode is doing. Drags keep a copy of the geometry from when they started, and redo their change
// to it every frame, so moving the mouse back undoes what it did
struct EditState
{
    EditTool tool = TOOL_SELECT;
    int selectedMesh = -1;
    GeoPick hover;

    enum Drag { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_EXTRUDE, DRAG_PUSHPULL, DRAG_EDGES, DRAG_PIVOT } drag = DRAG_NONE;
    bool dragMoved = false;           // Past a few pixels: a drag, not a click
    Vector2 dragStartMouse = { 0.0f, 0.0f };
    Geometry before;                  // The geometry when the drag started
    GeoPick dragPick;                 // What the drag started on
    Vector3 planePoint = { 0.0f, 0.0f, 0.0f }, planeNormal = { 0.0f, 1.0f, 0.0f };  // The mouse moves things along this plane...
    Vector3 dragStart = { 0.0f, 0.0f, 0.0f };   // ...from where it met it when the drag started
    bool dragVertical = false;        // Alt: up and down instead of along the floor
    bool duplicated = false;          // Ctrl: the drag moves a copy (of edges: extrudes them)

    // Create Cube: a rectangle on a plane, then how deep
    int cubeStage = 0;                // 0 waiting for a click, 1 drawing the rectangle, 2 setting the depth, 3 ready for Enter
    Vector3 cubeCorner = { 0.0f, 0.0f, 0.0f }, cubeU = { 1.0f, 0.0f, 0.0f }, cubeV = { 0.0f, 0.0f, 1.0f }, cubeNormal = { 0.0f, 1.0f, 0.0f };
    float cubeSizeU = 0.0f, cubeSizeV = 0.0f, cubeDepth = 0.0f;

    // Cut: the face and a point on it, then which way the cut runs
    int cutStage = 0;                 // 0 waiting, 1 picking the direction, 2 ready for Enter
    GeoPick cutPick;
    Vector3 cutDirection = { 1.0f, 0.0f, 0.0f };

    // Extrude around a pivot (P while extruding)
    bool pivotPlaced = false;
    bool pivotLocked = false;         // Once a face is extruded around it, it stays put
    Vector3 pivot = { 0.0f, 0.0f, 0.0f };
    int extrudeSegments = 0;          // Around a pivot: 0 picks one per 15 degrees

    // Edges, and bevelling them
    std::vector<GeoEdge> edges;
    std::vector<GeoEdge> extrudedEdges;  // While Ctrl+dragging edges: their copies, selected once it's done
    GeoEdge pressedEdge = { -1, 0, 0 };  // The edge Ctrl+pressed on: a click without dragging deselects it
    bool beveling = false;
    float bevelWidth = 0.0f;
    int bevelSegments = 1;
    Vector2 bevelStartMouse = { 0.0f, 0.0f };
};

// What Texturing mode is doing (texturingmode.cpp): the faces selected, and the one under the mouse
struct TextureState
{
    std::vector<GeoPick> faces;
    GeoPick hover;
};

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

    // The level's world, built from its geometry and drawn with the game's lighting
    World world;
    bool worldLoaded;
    int geometryVersion;       // Goes up whenever level.geometry changes (GeometryChanged)
    int builtGeometryVersion;  // The version the world was last built from
    bool builtFull;            // ...and whether that build had collision and the lightmap layout

    EditorMode mode;
    EditState edit;
    TextureState texturing;
    ViewMode viewMode;
    Sun defaultSun;           // Sun the world gets when the level has no "sun" entity
    Skybox skybox;            // The level's info.skybox
    std::string skyboxTried;  // Path last loaded (or tried), so a broken one isn't retried every frame

    // Fly camera: hold the right mouse button to look around, WASD/Q/E to move
    Camera camera;
    float yaw;
    float pitch;
    bool looking;

    // The right mouse button looks around once it's held a moment or the mouse moves. A quick press and
    // release without moving is a right click instead: rightClicked is true for the frame it's let go
    bool rightHeld;           // Pressed over the 3D view, and not let go or looking yet
    Vector2 rightPressMouse;
    double rightPressTime;
    bool rightClicked;

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
    bool keysFree;            // No dialog or text field had the keyboard last frame: tool keys can act
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
bool ImportObjMeshes(EditorState &editor, const std::string &path, float scale);  // Adds an OBJ's objects to the geometry as meshes
void ImportObjFromDialog(EditorState &editor);   // Asks which OBJ, then imports it at scale 1 (Quake units)
void CreateEntity(EditorState &editor, const char *classname);
void ShowCreateEntityList(EditorState &editor);                 // Deselects, so the details panel shows the list of entity types
void DeleteSelectedEntity(EditorState &editor);
void RotateSelectedEntity(EditorState &editor, float degrees);  // Positive turns left, seen from above
void ChangeGridSize(EditorState &editor, int steps);             // Steps through the grid sizes: -1 smaller, 1 bigger
float SnapToGrid(float value);                                   // The nearest grid line, or value as is with snapping off
void StartPointAt(EditorState &editor, int entityIndex, const std::string &parameter);
void CancelPointAt(EditorState &editor);
bool RaycastWorld(const EditorState &editor, Ray ray, Vector3 &point, Vector3 *normal);  // Where a ray first hits the level
void FrameCameraOnWorld(EditorState &editor);
void UpdateCameraFromAngles(EditorState &editor);
void AimCamera(EditorState &editor, Vector3 position, Vector3 target);  // Puts the camera there, looking at target

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

// Rebuilds the world from the level's geometry if it changed since, like after an undo. Call GeometryChanged first
void GeometryChanged(EditorState &editor);
void SyncWorldWithLevel(EditorState &editor);

// viewport.cpp: the 3D view
Rectangle GetViewportRect();
Ray GetViewportMouseRay(const EditorState &editor);  // The ray under the mouse
bool IsMouseOverViewport(const EditorState &editor);
void UpdateViewport(EditorState &editor);   // Camera movement and clicking entities
void DrawViewport(EditorState &editor);     // World, grid and entity markers. Call between BeginDrawing/EndDrawing
void UnloadViewport();
Texture2D GetViewportTexture();             // What DrawViewport drew last, upside down like render textures are

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
std::string GetPickedMaterial();  // The texture picked in the Materials tab, empty for none

// modellingmode.cpp: Modelling mode, its tools, the modes, and the toolbar and view menu over the 3D view
void SetEditorMode(EditorState &editor, EditorMode mode);  // Going back to Object mode builds collision and the lightmap layout
void CycleEditorMode(EditorState &editor);                 // Tab: Object, Modelling, Texturing, then Object again
const char *EditorModeName(EditorMode mode);               // Like "Modelling"
void ResetEditTools(EditorState &editor);
void UpdateModellingMode(EditorState &editor, bool keysFree, bool mouseFree);  // In Modelling mode, instead of entity selection
bool ModellingModeEscape(EditorState &editor);  // Cancels what the tool is doing, then the tool. False if there was nothing to
GeoPick PickGeometry(const EditorState &editor, Ray ray);  // The face a ray hits first
void DrawModellingOverlay(const EditorState &editor);  // Inside BeginMode3D, after the world
void DrawViewportUI(EditorState &editor);             // Over the 3D view: the mode and tool buttons, and the view menu
bool IsMouseOverViewportUI();                         // So clicks on them don't reach the level

// texturingmode.cpp: Texturing mode
void UpdateTexturingMode(EditorState &editor, bool keysFree, bool mouseFree);
bool TexturingModeEscape(EditorState &editor);   // Deselects the faces. False if none were
void DrawTexturingOverlay(const EditorState &editor);  // Inside BeginMode3D, after the world
const char *TexturingHint();                      // The controls, for the bar over the 3D view
