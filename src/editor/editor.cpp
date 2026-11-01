#include "editor.h"
#include "config.h"
#include "entitylist.h"
#include "filedialog.h"
#include "bounce.h"
#include "raymath.h"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>

static const char *LEVEL_FILTER = "Level files (*.json)\0*.json\0All files (*.*)\0*.*\0";
static const float DEFAULT_WORLD_SCALE = 1.0f;  // Imported OBJs are made in Quake units, like the world

// Grid sizes in world units (Quake units), where the player is 32 wide and 56 tall, like Quake's 1 to 256.
// Powers of two, so every size lines up with all the bigger ones
static const float GRID_SIZES[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f };
static const int GRID_SIZE_COUNT = (int)(sizeof(GRID_SIZES)/sizeof(GRID_SIZES[0]));

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

void AimCamera(EditorState &editor, Vector3 position, Vector3 target)
{
    Vector3 direction = Vector3Normalize(Vector3Subtract(target, position));
    editor.camera.position = position;
    editor.pitch = asinf(Clamp(direction.y, -1.0f, 1.0f));
    editor.yaw = atan2f(-direction.x, -direction.z);
    UpdateCameraFromAngles(editor);
}

void FrameCameraOnWorld(EditorState &editor)
{
    if (editor.level.geometry.meshes.empty())
    {
        AimCamera(editor, { 192.0f, 160.0f, 256.0f }, { 0.0f, 0.0f, 0.0f });
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
}

// A new, empty world for the level, then built from its geometry
static void StartWorld(EditorState &editor)
{
    UnloadCurrentWorld(editor);
    CreateWorld(editor.world);
    SetLightmapSettings(editor.world, editor.level.info.luxelSize, editor.level.info.lightmapFiltered);  // Before building, so it's laid out once
    editor.defaultSun = editor.world.lighting.sun;
    editor.worldLoaded = true;
    editor.mode = MODE_OBJECT;
    GeometryChanged(editor);
    SyncWorldWithLevel(editor);
}

void GeometryChanged(EditorState &editor)
{
    editor.geometryVersion++;
}

void SyncWorldWithLevel(EditorState &editor)
{
    // In Object mode the build is full: collision and the lightmap layout too, which starts a bake. Modelling and
    // Texturing mode build quickly, without them
    bool full = (editor.mode == MODE_OBJECT);
    if (editor.builtGeometryVersion == editor.geometryVersion && editor.builtFull == full) return;
    SetWorldGeometry(editor.world, editor.level.geometry, (float)editor.level.info.texelsPerUnit, full);
    editor.builtGeometryVersion = editor.geometryVersion;
    editor.builtFull = full;
}

static void NewLevel(EditorState &editor)
{
    editor.level = Level();
    editor.level.info.name = "Untitled";
    editor.level.info.entityCount = 0;
    editor.levelPath.clear();
    editor.selected = -1;
    ResetEditTools(editor);
    ResetUndoHistory(editor);
    StartWorld(editor);

    FrameCameraOnWorld(editor);
    SetStatus(editor, "New level. Press Tab for Modelling mode to build its geometry");
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

    editor.level = level;
    editor.levelPath = path;
    editor.selected = -1;
    ResetEditTools(editor);
    ResetUndoHistory(editor);
    StartWorld(editor);

    FrameCameraOnWorld(editor);
    SetStatus(editor, "Opened %s, %d meshes", ToDataPath(editor, path).c_str(), (int)editor.level.geometry.meshes.size());
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
    editor.rightHeld = false;
    editor.rightPressMouse = { 0.0f, 0.0f };
    editor.rightPressTime = 0.0;
    editor.rightClicked = false;
    editor.dragging = false;
    editor.dragMoved = false;
    editor.dragVertical = false;
    editor.resizeFace = -1;
    editor.pointing = false;
    editor.pointEntity = -1;
    editor.nextStateId = 0;
    editor.mode = MODE_OBJECT;
    editor.geometryVersion = 0;
    editor.builtGeometryVersion = -1;
    editor.builtFull = false;
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
    // Geometry being modelled or textured isn't built for good: going back to Object mode works out its collision
    // and lightmap, which the save needs
    if (editor.mode != MODE_OBJECT) SetEditorMode(editor, MODE_OBJECT);

    std::string path = editor.levelPath;
    if (askForPath || path.empty())
    {
        if (!ShowSaveFileDialog(GetWindowHandle(), "Save Level", LEVEL_FILTER, "json", editor.dataRoot + "/maps", path)) return false;
    }

    // The geometry as the file will have it (rounded), built from that. An undo can bring back geometry from the
    // middle of an edit that wasn't rounded yet. Rebuilding the same geometry keeps the lightmap it has
    GeoQuantize(editor.level.geometry);
    GeometryChanged(editor);
    SyncWorldWithLevel(editor);

    // The bounce lightmap goes next to it, so the game doesn't have to bake it. It has to match the world
    // and sun being saved: finish the bake for them, or bake them now if none has started yet
    World &world = editor.world;
    bool saveLightmap = false;
    if (editor.worldLoaded && !world.lightmapTexels.empty())
    {
        unsigned long long key = LightmapKey(world);
        if (world.lightmapKey != key)
        {
            if (LightmapBakingKey(world) != key) StartLightmapBake(world);
            FinishLightmapBake(world, nullptr);
        }
        saveLightmap = (world.lightmapKey == key);
    }
    editor.level.info.lightmapKey = saveLightmap ? LightmapKeyToText(world.lightmapKey) : "";

    if (!SaveLevel(editor.level, path.c_str()))
    {
        SetStatus(editor, "Couldn't save %s", path.c_str());
        return false;
    }

    editor.levelPath = path;
    MarkSaved(editor);

    std::string lightmapPath = LightmapPath(path);
    if (saveLightmap && SaveLightmap(world, lightmapPath.c_str()))
    {
        SetStatus(editor, "Saved %s and its lightmap", ToDataPath(editor, path).c_str());
    }
    else
    {
        SetStatus(editor, "Saved %s", ToDataPath(editor, path).c_str());
    }
    return true;
}

bool ImportObjMeshes(EditorState &editor, const std::string &path, float scale)
{
    Geometry imported;
    if (!ImportObjGeometry(path.c_str(), scale, (float)editor.level.info.texelsPerUnit, imported) || imported.meshes.empty())
    {
        SetStatus(editor, "Couldn't import %s, see the log for why", path.c_str());
        return false;
    }
    GeoQuantize(imported);  // As saving will store it

    // Added to the level's geometry, shapes and all
    RecordUndo(editor, "Import " + path);
    int shapeBase = (int)editor.level.geometry.shapes.size();
    for (const GeoShape &shape : imported.shapes) editor.level.geometry.shapes.push_back(shape);
    for (GeoMesh mesh : imported.meshes)
    {
        mesh.shape += shapeBase;
        editor.level.geometry.meshes.push_back(mesh);
    }
    GeometryChanged(editor);
    SyncWorldWithLevel(editor);
    FrameCameraOnWorld(editor);
    SetStatus(editor, "Imported %s as %d meshes", path.c_str(), (int)imported.meshes.size());
    return true;
}

void ImportObjFromDialog(EditorState &editor)
{
    std::string path;
    if (!ShowOpenFileDialog(GetWindowHandle(), "Import OBJ as Meshes", "OBJ models (*.obj)\0*.obj\0All files (*.*)\0*.*\0", editor.dataRoot + "/models", path)) return;
    SetEditorMode(editor, MODE_MODELLING);  // Geometry only changes in Modelling mode
    ImportObjMeshes(editor, ToDataPath(editor, path), DEFAULT_WORLD_SCALE);
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
    return Vector3Add(ray.position, Vector3Scale(ray.direction, 128.0f));
}

void StartPointAt(EditorState &editor, int entityIndex, const std::string &parameter)
{
    if (entityIndex < 0 || entityIndex >= (int)editor.level.entities.size()) return;
    if (!editor.worldLoaded)
    {
        SetStatus(editor, "Point At needs geometry to click on: make some in Modelling mode (Tab)");
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

void ShowCreateEntityList(EditorState &editor)
{
    editor.selected = -1;
    CancelPointAt(editor);
    SetStatus(editor, "Pick an entity type under Create Entity in the details panel");
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
    entity.origin.x = SnapToGrid(entity.origin.x);  // Not the height, which keeps it just off the surface it's placed on
    entity.origin.z = SnapToGrid(entity.origin.z);
    entity.properties = nlohmann::ordered_json::object();
    for (const EntityParamInfo &param : entityClass->params) entity.properties[param.name] = DefaultParamValue(param);

    RecordUndo(editor, std::string("Create ") + classname);
    editor.level.entities.push_back(entity);
    editor.selected = (int)editor.level.entities.size() - 1;
    SetStatus(editor, "Created %s", classname);
}

void ChangeGridSize(EditorState &editor, int steps)
{
    // Start from the size nearest the current one, in case editor.cfg was edited to one that isn't in the list
    int index = 0;
    for (int i = 1; i < GRID_SIZE_COUNT; i++)
    {
        if (fabsf(GRID_SIZES[i] - editorSettings.gridSize) < fabsf(GRID_SIZES[index] - editorSettings.gridSize)) index = i;
    }

    index += steps;
    if (index < 0) index = 0;
    if (index > GRID_SIZE_COUNT - 1) index = GRID_SIZE_COUNT - 1;
    editorSettings.gridSize = GRID_SIZES[index];
    MarkSettingsChanged();

    if (editorSettings.snapToGrid) SetStatus(editor, "Grid size %g", editorSettings.gridSize);
    else SetStatus(editor, "Grid size %g. Snapping is off, see Edit > Preferences", editorSettings.gridSize);
}

float SnapToGrid(float value)
{
    if (!editorSettings.snapToGrid || editorSettings.gridSize <= 0.0f) return value;
    return roundf(value/editorSettings.gridSize)*editorSettings.gridSize;
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

void DeleteSelectedEntity(EditorState &editor)
{
    if (editor.selected < 0 || editor.selected >= (int)editor.level.entities.size()) return;

    std::string classname = editor.level.entities[editor.selected].classname;
    RecordUndo(editor, "Delete " + classname);
    editor.level.entities.erase(editor.level.entities.begin() + editor.selected);
    editor.selected = -1;
    SetStatus(editor, "Deleted %s", classname.c_str());
}
