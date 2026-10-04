#include "editor.h"
#include "entitylist.h"
#include "filedialog.h"
#include "raymath.h"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>

static const char *LEVEL_FILTER = "Level files (*.json)\0*.json\0All files (*.*)\0*.*\0";
static const float DEFAULT_WORLD_SCALE = 1.0f/32.0f;  // Quake units to world units, same as the game uses

void SetStatus(EditorState &editor, const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    editor.status = buffer;
    editor.statusTime = GetTime();
    TraceLog(LOG_INFO, "EDITOR: %s", buffer);
}

std::string ToDataPath(const EditorState &editor, const std::string &path)
{
    std::string result = path;
    for (char &c : result) if (c == '\\') c = '/';

    std::string root = editor.dataRoot;
    for (char &c : root) if (c == '\\') c = '/';
    if (!root.empty() && root.back() != '/') root += '/';

    if (result.size() > root.size() && _strnicmp(result.c_str(), root.c_str(), root.size()) == 0) return result.substr(root.size());
    return result;
}

// Points the camera along its yaw and pitch, same convention as the game's player: yaw 0 looks down -Z
void UpdateCameraFromAngles(EditorState &editor)
{
    Vector3 look = {
        -sinf(editor.yaw)*cosf(editor.pitch),
        sinf(editor.pitch),
        -cosf(editor.yaw)*cosf(editor.pitch)
    };
    editor.camera.target = Vector3Add(editor.camera.position, look);
}

static void AimCamera(EditorState &editor, Vector3 position, Vector3 target)
{
    Vector3 direction = Vector3Normalize(Vector3Subtract(target, position));
    editor.camera.position = position;
    editor.pitch = asinf(Clamp(direction.y, -1.0f, 1.0f));
    editor.yaw = atan2f(-direction.x, -direction.z);
    UpdateCameraFromAngles(editor);
}

void FrameCameraOnWorld(EditorState &editor)
{
    if (!editor.worldLoaded)
    {
        AimCamera(editor, { 6.0f, 5.0f, 8.0f }, { 0.0f, 0.0f, 0.0f });
        return;
    }

    // Look at the middle of the level from above one of its corners
    BoundingBox b = editor.world.bounds;
    Vector3 center = Vector3Scale(Vector3Add(b.min, b.max), 0.5f);
    float size = Vector3Length(Vector3Subtract(b.max, b.min));
    AimCamera(editor, Vector3Add(center, { size*0.45f, size*0.4f, size*0.45f }), center);
}

static void UnloadCurrentWorld(EditorState &editor)
{
    if (editor.worldLoaded) UnloadWorld(editor.world);
    editor.world = World();
    editor.worldLoaded = false;
    editor.loadedModel.clear();
}

// Loads a world model in place of the current one. If it fails, the current one stays
static bool SwapWorld(EditorState &editor, const std::string &model, float scale)
{
    World world;
    if (!LoadWorld(world, model.c_str(), scale)) return false;

    UnloadCurrentWorld(editor);
    editor.world = world;
    editor.worldLoaded = true;
    editor.loadedModel = model;
    editor.loadedScale = scale;
    editor.defaultSun = editor.world.lighting.sun;
    return true;
}

void SyncWorldWithLevel(EditorState &editor)
{
    const Level &level = editor.level;
    if (level.worldModel.empty())
    {
        UnloadCurrentWorld(editor);
        return;
    }

    bool matches = editor.worldLoaded && editor.loadedModel == level.worldModel && editor.loadedScale == level.worldScale;
    if (!matches && !SwapWorld(editor, level.worldModel, level.worldScale)) SetStatus(editor, "Couldn't load %s", level.worldModel.c_str());
}

static void NewLevel(EditorState &editor)
{
    UnloadCurrentWorld(editor);

    editor.level = Level();
    editor.level.info.name = "Untitled";
    editor.level.info.entityCount = 0;
    editor.level.worldScale = DEFAULT_WORLD_SCALE;
    editor.levelPath.clear();
    editor.selected = -1;
    ResetUndoHistory(editor);

    FrameCameraOnWorld(editor);
    SetStatus(editor, "New level");
}

static void OpenLevel(EditorState &editor)
{
    std::string path;
    if (!ShowOpenFileDialog(GetWindowHandle(), "Open Level", LEVEL_FILTER, editor.dataRoot + "/maps", path)) return;

    Level level;
    if (!LoadLevel(level, path.c_str()))
    {
        SetStatus(editor, "Couldn't open %s, see the log for why", path.c_str());
        return;
    }

    UnloadCurrentWorld(editor);
    editor.level = level;
    editor.levelPath = path;
    editor.selected = -1;
    ResetUndoHistory(editor);

    if (!editor.level.worldModel.empty()) SwapWorld(editor, editor.level.worldModel, editor.level.worldScale);

    FrameCameraOnWorld(editor);
    if (!editor.level.worldModel.empty() && !editor.worldLoaded) SetStatus(editor, "Opened %s, but its world model %s didn't load", ToDataPath(editor, path).c_str(), editor.level.worldModel.c_str());
    else SetStatus(editor, "Opened %s", ToDataPath(editor, path).c_str());
}

void InitEditor(EditorState &editor, const std::string &dataRoot)
{
    editor.dataRoot = dataRoot;
    editor.worldLoaded = false;
    editor.camera = { 0 };
    editor.camera.up = { 0.0f, 1.0f, 0.0f };
    editor.camera.fovy = 70.0f;
    editor.camera.projection = CAMERA_PERSPECTIVE;
    editor.looking = false;
    editor.dragging = false;
    editor.dragMoved = false;
    editor.dragVertical = false;
    editor.pointing = false;
    editor.pointEntity = -1;
    editor.nextStateId = 0;
    editor.loadedScale = 0.0f;
    editor.openMenu = MENU_NONE;
    editor.dialog = DIALOG_NONE;
    editor.pendingAction = ACTION_NONE;
    editor.textEditing = false;
    editor.quit = false;
    editor.statusTime = 0.0;

    NewLevel(editor);
}

void ShutdownEditor(EditorState &editor)
{
    UnloadCurrentWorld(editor);
    UnloadSkybox(editor.skybox);
}

void RunAction(EditorState &editor, PendingAction action)
{
    switch (action)
    {
        case ACTION_NEW_LEVEL: NewLevel(editor); break;
        case ACTION_OPEN_LEVEL: OpenLevel(editor); break;
        case ACTION_EXIT: editor.quit = true; break;
        default: break;
    }
}

void RequestAction(EditorState &editor, PendingAction action)
{
    if (editor.unsavedChanges)
    {
        editor.pendingAction = action;
        OpenDialog(editor, DIALOG_UNSAVED_CHANGES);
    }
    else
    {
        RunAction(editor, action);
    }
}

bool SaveCurrentLevel(EditorState &editor, bool askForPath)
{
    std::string path = editor.levelPath;
    if (askForPath || path.empty())
    {
        if (!ShowSaveFileDialog(GetWindowHandle(), "Save Level", LEVEL_FILTER, "json", editor.dataRoot + "/maps", path)) return false;
    }

    if (!SaveLevel(editor.level, path.c_str()))
    {
        SetStatus(editor, "Couldn't save %s", path.c_str());
        return false;
    }

    editor.levelPath = path;
    MarkSaved(editor);
    SetStatus(editor, "Saved %s", ToDataPath(editor, path).c_str());
    return true;
}

bool CreateWorld(EditorState &editor, const std::string &model, float scale)
{
    if (!SwapWorld(editor, model, scale))
    {
        SetStatus(editor, "Couldn't load %s as a world model", model.c_str());
        return false;
    }

    RecordUndo(editor, "Set world model");
    editor.level.worldModel = model;
    editor.level.worldScale = scale;

    FrameCameraOnWorld(editor);
    SetStatus(editor, "World model set to %s", model.c_str());
    return true;
}

bool RaycastWorld(const EditorState &editor, Ray ray, Vector3 &point, Vector3 *normal)
{
    if (!editor.worldLoaded) return false;

    RayCollision closest = { 0 };
    for (int i = 0; i < editor.world.model.meshCount; i++)
    {
        RayCollision hit = GetRayCollisionMesh(ray, editor.world.model.meshes[i], editor.world.model.transform);
        if (hit.hit && (!closest.hit || hit.distance < closest.distance)) closest = hit;
    }
    if (!closest.hit) return false;

    point = closest.point;
    if (normal) *normal = closest.normal;
    return true;
}

// Where a new entity goes: on the level surface in the middle of the view, or a few units in front of
// the camera if the middle of the view is empty. offset keeps it clear of the surface
static Vector3 PlacementPoint(const EditorState &editor, float offset)
{
    Ray ray = { editor.camera.position, Vector3Normalize(Vector3Subtract(editor.camera.target, editor.camera.position)) };

    Vector3 point, normal;
    if (RaycastWorld(editor, ray, point, &normal)) return Vector3Add(point, Vector3Scale(normal, offset));
    return Vector3Add(ray.position, Vector3Scale(ray.direction, 4.0f));
}

void StartPointAt(EditorState &editor, int entityIndex, const std::string &parameter)
{
    if (entityIndex < 0 || entityIndex >= (int)editor.level.entities.size()) return;
    if (!editor.worldLoaded)
    {
        SetStatus(editor, "Point At needs a world model to click on, see Edit > Create World");
        return;
    }

    editor.pointing = true;
    editor.pointEntity = entityIndex;
    editor.pointParameter = parameter;
    SetStatus(editor, "Click a spot on the level to point the %s's %s at it. Esc cancels",
              editor.level.entities[entityIndex].classname.c_str(), parameter.c_str());
}

void CancelPointAt(EditorState &editor)
{
    if (!editor.pointing) return;
    editor.pointing = false;
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
}

void CreateEntity(EditorState &editor, const char *classname)
{
    // Everything about the type comes from entitylist.txt, written by the game
    const EntityClassInfo *entityClass = FindEntityClass(classname);
    if (entityClass == nullptr)
    {
        SetStatus(editor, "%s isn't in entitylist.txt", classname);
        return;
    }

    LevelEntity entity;
    entity.classname = classname;
    entity.origin = PlacementPoint(editor, entityClass->placement);
    entity.properties = nlohmann::ordered_json::object();
    for (const EntityParamInfo &param : entityClass->params) entity.properties[param.name] = DefaultParamValue(param);

    RecordUndo(editor, std::string("Create ") + classname);
    editor.level.entities.push_back(entity);
    editor.selected = (int)editor.level.entities.size() - 1;
    SetStatus(editor, "Created %s", classname);
}

// Rounds to 4 decimals, so saved values read 0.4653 instead of 0.46530000123
static double Tidy(double value)
{
    return std::round(value*10000.0)/10000.0;
}

void RotateSelectedEntity(EditorState &editor, float degrees)
{
    if (editor.selected < 0 || editor.selected >= (int)editor.level.entities.size()) return;
    LevelEntity &entity = editor.level.entities[editor.selected];

    // Whichever parameter entitylist.txt says is a direction or an angle
    const EntityClassInfo *entityClass = FindEntityClass(entity.classname);
    const EntityParamInfo *direction = entityClass ? FindEntityParamOfType(*entityClass, "direction") : nullptr;
    const EntityParamInfo *angle = entityClass ? FindEntityParamOfType(*entityClass, "angle") : nullptr;
    if (direction == nullptr && angle == nullptr)
    {
        SetStatus(editor, "%s has no angle or direction to rotate", entity.classname.c_str());
        return;
    }

    RecordUndo(editor, "Rotate " + entity.classname);
    if (direction != nullptr)
    {
        // Swing the direction around the vertical axis, the same way turning changes the player's angle
        Vector3 d = EntityVectorOrDefault(entity, direction->name.c_str(), { 0.0f, -1.0f, 0.0f });
        float r = degrees*DEG2RAD;
        entity.properties[direction->name] = { Tidy(d.x*cosf(r) + d.z*sinf(r)), Tidy(d.y), Tidy(-d.x*sinf(r) + d.z*cosf(r)) };
    }
    else
    {
        float value = fmodf(EntityNumberOrDefault(entity, angle->name.c_str(), 0.0f) + degrees, 360.0f);
        if (value < 0.0f) value += 360.0f;
        if (value == floorf(value)) entity.properties[angle->name] = (int)value;
        else entity.properties[angle->name] = Tidy(value);
    }

    SetStatus(editor, "Rotated %s %g degrees %s", entity.classname.c_str(), fabsf(degrees), (degrees > 0.0f) ? "left" : "right");
}

bool ReloadWorld(EditorState &editor)
{
    if (editor.level.worldModel.empty())
    {
        SetStatus(editor, "This level has no world model to reload yet, see Edit > Create World");
        return false;
    }

    // SwapWorld loads the new one before letting go of the old one, so a broken .obj doesn't leave nothing to look at
    if (!SwapWorld(editor, editor.level.worldModel, editor.level.worldScale))
    {
        SetStatus(editor, "Couldn't reload %s, kept the previous version", editor.level.worldModel.c_str());
        return false;
    }

    // The level only stores the model's path, so it doesn't change: saving it picks up the new mesh as is
    SetStatus(editor, "Reloaded %s, %d collision brushes", editor.level.worldModel.c_str(), (int)editor.world.colliders.size());
    return true;
}

void DeleteSelectedEntity(EditorState &editor)
{
    if (editor.selected < 0 || editor.selected >= (int)editor.level.entities.size()) return;

    std::string classname = editor.level.entities[editor.selected].classname;
    RecordUndo(editor, "Delete " + classname);
    editor.level.entities.erase(editor.level.entities.begin() + editor.selected);
    editor.selected = -1;
    SetStatus(editor, "Deleted %s", classname.c_str());
}
