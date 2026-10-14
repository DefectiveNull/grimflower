#include "editor.h"
#include "raymath.h"
#include "rlgl.h"
#include "bounce.h"
#include "config.h"
#include "entitylist.h"
#include "style.h"

#include <cmath>

static const float FAST_MULTIPLIER = 3.0f;     // While +speed is held
static const float MAX_PITCH = 89.0f*DEG2RAD;

Rectangle GetViewportRect()
{
    return { 0.0f, (float)MENU_BAR_HEIGHT, (float)(GetScreenWidth() - DETAILS_PANEL_WIDTH),
             (float)(GetScreenHeight() - MENU_BAR_HEIGHT - STATUS_BAR_HEIGHT) };
}

bool IsMouseOverViewport(const EditorState &editor)
{
    if (editor.openMenu != MENU_NONE || editor.dialog != DIALOG_NONE) return false;
    return CheckCollisionPointRec(GetMousePosition(), GetViewportRect());
}

// How big an entity's marker is, for clicking on it
static float PickRadius(const LevelEntity &entity)
{
    const EntityClassInfo *entityClass = FindEntityClass(entity.classname);
    if (entityClass == nullptr) return 0.35f;
    if (entityClass->shape == "volume") return 0.35f;  // Just the handle at its origin, so a big volume doesn't block clicks on what's inside
    if (entityClass->shape == "box") return Vector3Length(Vector3Subtract(entityClass->maxs, entityClass->mins))*0.4f;
    return fmaxf(entityClass->radius*1.2f, 0.35f);
}

static void UpdateCamera(EditorState &editor)
{
    // Hold the right mouse button in the viewport to look around and fly
    if (!editor.looking && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && IsMouseOverViewport(editor))
    {
        editor.looking = true;
        DisableCursor();
    }
    if (editor.looking && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        editor.looking = false;
        EnableCursor();
    }

    float dt = GetFrameTime();
    Vector3 forward = Vector3Normalize(Vector3Subtract(editor.camera.target, editor.camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, { 0.0f, 1.0f, 0.0f }));

    if (editor.looking)
    {
        Vector2 delta = GetMouseDelta();
        editor.yaw -= delta.x*editorSettings.lookSensitivity;
        float pitchDirection = editorSettings.invertLook ? -1.0f : 1.0f;
        editor.pitch = Clamp(editor.pitch - pitchDirection*delta.y*editorSettings.lookSensitivity, -MAX_PITCH, MAX_PITCH);

        // Buttons come from editor.cfg's binds
        float speed = editorSettings.moveSpeed*(cameraButtons.speed ? FAST_MULTIPLIER : 1.0f)*dt;
        Vector3 move = { 0 };
        if (cameraButtons.forward) move = Vector3Add(move, forward);
        if (cameraButtons.back) move = Vector3Subtract(move, forward);
        if (cameraButtons.moveRight) move = Vector3Add(move, right);
        if (cameraButtons.moveLeft) move = Vector3Subtract(move, right);
        if (cameraButtons.moveUp) move.y += 1.0f;
        if (cameraButtons.moveDown) move.y -= 1.0f;
        editor.camera.position = Vector3Add(editor.camera.position, Vector3Scale(move, speed));
    }

    // Mouse wheel moves forward and back
    if (IsMouseOverViewport(editor))
    {
        float wheel = GetMouseWheelMove();
        editor.camera.position = Vector3Add(editor.camera.position, Vector3Scale(forward, wheel*2.0f));
    }

    editor.camera.fovy = editorSettings.fieldOfView;
    UpdateCameraFromAngles(editor);
}

// The ray under the mouse. The 3D view is rendered at the viewport's size, so work in its coordinates
static Ray MouseRay(const EditorState &editor)
{
    Rectangle view = GetViewportRect();
    Vector2 mouse = { GetMousePosition().x - view.x, GetMousePosition().y - view.y };
    return GetScreenToWorldRayEx(mouse, editor.camera, (int)view.width, (int)view.height);
}

// Where a ray meets a plane, if it does in front of the ray's start
static bool RayPlane(Ray ray, Vector3 planePoint, Vector3 planeNormal, Vector3 &hit)
{
    float facing = Vector3DotProduct(ray.direction, planeNormal);
    if (fabsf(facing) < 0.0001f) return false;  // Running along the plane

    float distance = Vector3DotProduct(Vector3Subtract(planePoint, ray.position), planeNormal)/facing;
    if (distance < 0.0f || distance > 1000.0f) return false;  // Behind the camera, or too far off to be useful
    hit = Vector3Add(ray.position, Vector3Scale(ray.direction, distance));
    return true;
}

static bool IsAltDown()
{
    return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
}

// Sets up the plane the entity slides along, through where it is now: flat for horizontal moves, or
// standing up and facing the camera for vertical ones. Called again if Alt changes mid-drag
static void StartDragPlane(EditorState &editor, Ray ray)
{
    Vector3 origin = editor.level.entities[editor.selected].origin;
    editor.dragVertical = IsAltDown();
    editor.dragPlanePoint = origin;

    if (editor.dragVertical)
    {
        Vector3 toCamera = Vector3Subtract(editor.camera.position, origin);
        toCamera.y = 0.0f;
        editor.dragPlaneNormal = (Vector3LengthSqr(toCamera) > 0.0001f) ? Vector3Normalize(toCamera) : Vector3{ 0.0f, 0.0f, 1.0f };
    }
    else
    {
        editor.dragPlaneNormal = { 0.0f, 1.0f, 0.0f };
    }

    Vector3 hit;
    editor.dragOffset = RayPlane(ray, editor.dragPlanePoint, editor.dragPlaneNormal, hit) ? Vector3Subtract(origin, hit) : Vector3{ 0 };
}

// Volumes: entities with their own box, from their mins and maxs parameters
static bool IsVolume(const LevelEntity &entity)
{
    const EntityClassInfo *entityClass = FindEntityClass(entity.classname);
    return entityClass != nullptr && entityClass->shape == "volume";
}

// Whether the View menu shows this entity. Hidden ones aren't drawn, labelled or clickable in the view
static bool IsEntityShown(const LevelEntity &entity)
{
    return IsVolume(entity) ? editorSettings.showVolumes : editorSettings.showPointEntities;
}

static BoundingBox VolumeBox(const LevelEntity &entity)
{
    Vector3 mins = EntityVectorOrDefault(entity, "mins", { -0.5f, -0.5f, -0.5f });
    Vector3 maxs = EntityVectorOrDefault(entity, "maxs", { 0.5f, 0.5f, 0.5f });
    return { Vector3Add(entity.origin, Vector3Min(mins, maxs)), Vector3Add(entity.origin, Vector3Max(mins, maxs)) };
}

static float &Axis(Vector3 &v, int axis) { return (axis == 0) ? v.x : (axis == 1) ? v.y : v.z; }

// The way a face of a box points: faces 0-5 are -x, +x, -y, +y, -z, +z
static Vector3 FaceNormal(int face)
{
    Vector3 normal = { 0.0f, 0.0f, 0.0f };
    Axis(normal, face/2) = (face%2) ? 1.0f : -1.0f;
    return normal;
}

// How big a resize handle is drawn: the same size on screen however far away it is
static float HandleSize(const EditorState &editor, Vector3 position)
{
    return Vector3Distance(editor.camera.position, position)*0.018f;
}

// Where a face's resize handle is: just outside the middle of the face
static Vector3 HandlePosition(const EditorState &editor, BoundingBox box, int face)
{
    Vector3 center = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
    Axis(center, face/2) = (face%2) ? Axis(box.max, face/2) : Axis(box.min, face/2);
    return Vector3Add(center, Vector3Scale(FaceNormal(face), HandleSize(editor, center)*0.75f));
}

// How far the mouse is from a point in the 3D view, in pixels. Points behind the camera are infinitely far
static float MouseDistance(const EditorState &editor, Vector3 point)
{
    Vector3 forward = Vector3Subtract(editor.camera.target, editor.camera.position);
    if (Vector3DotProduct(Vector3Subtract(point, editor.camera.position), forward) <= 0.0f) return INFINITY;
    Rectangle view = GetViewportRect();
    Vector2 screen = GetWorldToScreenEx(point, editor.camera, (int)view.width, (int)view.height);
    return Vector2Distance(GetMousePosition(), { view.x + screen.x, view.y + screen.y });
}

// The selected volume's resize handle under the mouse, or -1. Its origin handle wins where they overlap,
// like the bottom face's of a volume sitting on its origin, seen from above, so it can still be moved
static int HoveredResizeHandle(const EditorState &editor)
{
    if (editor.selected < 0 || editor.selected >= (int)editor.level.entities.size()) return -1;
    const LevelEntity &entity = editor.level.entities[editor.selected];
    if (!IsVolume(entity) || !IsEntityShown(entity)) return -1;

    BoundingBox box = VolumeBox(entity);
    int closest = -1;
    float closestDistance = 10.0f;  // Pixels
    for (int face = 0; face < 6; face++)
    {
        float distance = MouseDistance(editor, HandlePosition(editor, box, face));
        if (distance < closestDistance)
        {
            closest = face;
            closestDistance = distance;
        }
    }
    if (closest >= 0 && MouseDistance(editor, entity.origin) <= closestDistance) return -1;
    return closest;
}

// Sets up the plane the mouse moves the face along: through the face, containing its axis, and turned
// to face the camera as much as it can
static void StartResizePlane(EditorState &editor, Ray ray)
{
    LevelEntity &entity = editor.level.entities[editor.selected];
    BoundingBox box = VolumeBox(entity);
    int axis = editor.resizeFace/2;
    Vector3 normal = FaceNormal(editor.resizeFace);
    float face = (editor.resizeFace%2) ? Axis(box.max, axis) : Axis(box.min, axis);
    Vector3 point = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
    Axis(point, axis) = face;

    Vector3 toCamera = Vector3Subtract(editor.camera.position, point);
    Vector3 planeNormal = Vector3Subtract(toCamera, Vector3Scale(normal, Vector3DotProduct(toCamera, normal)));
    if (Vector3LengthSqr(planeNormal) < 0.0001f) planeNormal = (axis == 1) ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
    editor.dragPlanePoint = point;
    editor.dragPlaneNormal = Vector3Normalize(planeNormal);

    Vector3 hit;
    editor.resizeOffset = RayPlane(ray, point, editor.dragPlaneNormal, hit) ? face - Axis(hit, axis) : 0.0f;
}

// Dragging a face of the selected volume: it slides along its axis to the grid, and can't pass the opposite face
static void UpdateResize(EditorState &editor, Ray ray)
{
    LevelEntity &entity = editor.level.entities[editor.selected];
    if (!editor.dragMoved)
    {
        if (Vector2Distance(GetMousePosition(), editor.dragStartMouse) < 4.0f) return;
        editor.dragMoved = true;
        RecordUndo(editor, "Resize " + entity.classname);  // One undo step for the whole drag
        StartResizePlane(editor, ray);
    }

    Vector3 hit;
    if (!RayPlane(ray, editor.dragPlanePoint, editor.dragPlaneNormal, hit)) return;

    BoundingBox box = VolumeBox(entity);
    int axis = editor.resizeFace/2;
    bool isMax = (editor.resizeFace%2) != 0;
    float face = SnapToGrid(Axis(hit, axis) + editor.resizeOffset);

    // A face that would cross the opposite one, or leave the box flat, stays where it was
    const float MIN_SIZE = 0.0625f;
    if (isMax ? (face - Axis(box.min, axis) < MIN_SIZE) : (Axis(box.max, axis) - face < MIN_SIZE)) return;
    Axis(isMax ? box.max : box.min, axis) = face;

    // Saved relative to the origin, mins below maxs, rounded so the file doesn't fill up with long decimals
    auto round4 = [](float value) { return roundf(value*10000.0f)/10000.0; };
    Vector3 mins = Vector3Subtract(box.min, entity.origin), maxs = Vector3Subtract(box.max, entity.origin);
    entity.properties["mins"] = { round4(mins.x), round4(mins.y), round4(mins.z) };
    entity.properties["maxs"] = { round4(maxs.x), round4(maxs.y), round4(maxs.z) };
}

static void UpdateDrag(EditorState &editor)
{
    if (!editor.dragging) return;

    bool valid = editor.selected >= 0 && editor.selected < (int)editor.level.entities.size();
    if (valid && editor.resizeFace >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !editor.looking)
    {
        UpdateResize(editor, MouseRay(editor));
        return;
    }
    if (valid && editor.resizeFace >= 0)
    {
        if (editor.dragMoved)
        {
            BoundingBox box = VolumeBox(editor.level.entities[editor.selected]);
            Vector3 size = Vector3Subtract(box.max, box.min);
            SetStatus(editor, "Resized %s to %g x %g x %g", editor.level.entities[editor.selected].classname.c_str(), size.x, size.y, size.z);
        }
        editor.dragging = false;
        editor.resizeFace = -1;
        return;
    }

    if (!valid || !IsMouseButtonDown(MOUSE_BUTTON_LEFT) || editor.looking)
    {
        if (valid && editor.dragMoved)
        {
            const LevelEntity &entity = editor.level.entities[editor.selected];
            SetStatus(editor, "Moved %s to (%.2f %.2f %.2f)", entity.classname.c_str(), entity.origin.x, entity.origin.y, entity.origin.z);
        }
        editor.dragging = false;
        return;
    }

    // A few pixels of wiggle still count as a click, so selecting never nudges anything
    Ray ray = MouseRay(editor);
    if (!editor.dragMoved)
    {
        if (Vector2Distance(GetMousePosition(), editor.dragStartMouse) < 4.0f) return;
        editor.dragMoved = true;
        RecordUndo(editor, "Move " + editor.level.entities[editor.selected].classname);  // One undo step for the whole drag
        StartDragPlane(editor, ray);
    }
    if (IsAltDown() != editor.dragVertical) StartDragPlane(editor, ray);

    Vector3 hit;
    if (!RayPlane(ray, editor.dragPlanePoint, editor.dragPlaneNormal, hit)) return;

    Vector3 target = Vector3Add(hit, editor.dragOffset);
    Vector3 &origin = editor.level.entities[editor.selected].origin;
    if (editor.dragVertical)
    {
        origin.y = SnapToGrid(target.y);
    }
    else
    {
        origin.x = SnapToGrid(target.x);
        origin.z = SnapToGrid(target.z);
    }
}

// The entity being pointed, or nullptr if Point At should stop: the entity is gone, or something else got selected
static LevelEntity *PointingEntity(EditorState &editor)
{
    if (!editor.pointing) return nullptr;
    bool valid = editor.pointEntity >= 0 && editor.pointEntity < (int)editor.level.entities.size() && editor.selected == editor.pointEntity;
    if (!valid)
    {
        CancelPointAt(editor);
        return nullptr;
    }
    return &editor.level.entities[editor.pointEntity];
}

// Point At: a left click on the level aims the entity's direction parameter from its origin at the clicked spot
static void UpdatePointAt(EditorState &editor)
{
    LevelEntity *entity = PointingEntity(editor);
    if (entity == nullptr) return;

    SetMouseCursor(IsMouseOverViewport(editor) && !editor.looking ? MOUSE_CURSOR_CROSSHAIR : MOUSE_CURSOR_DEFAULT);
    if (editor.looking || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !IsMouseOverViewport(editor)) return;

    Vector3 target;
    if (!RaycastWorld(editor, MouseRay(editor), target, nullptr))
    {
        SetStatus(editor, "That's empty space, click on the level to point at it");
        return;
    }

    Vector3 toTarget = Vector3Subtract(target, entity->origin);
    if (Vector3Length(toTarget) < 0.001f)
    {
        SetStatus(editor, "That's where the %s is, click somewhere else", entity->classname.c_str());
        return;
    }

    Vector3 d = Vector3Normalize(toTarget);
    auto round4 = [](float value) { return roundf(value*10000.0f)/10000.0; };
    RecordUndo(editor, "Point " + entity->classname + " " + editor.pointParameter);
    entity->properties[editor.pointParameter] = { round4(d.x), round4(d.y), round4(d.z) };
    SetStatus(editor, "Pointed the %s at (%.2f %.2f %.2f)", entity->classname.c_str(), target.x, target.y, target.z);
    CancelPointAt(editor);
}

// While pointing: a line from the entity to the spot under the mouse, showing where it would point
static void DrawPointAtPreview(EditorState &editor)
{
    LevelEntity *entity = PointingEntity(editor);
    if (entity == nullptr || !IsMouseOverViewport(editor)) return;

    Vector3 target;
    if (!RaycastWorld(editor, MouseRay(editor), target, nullptr)) return;
    DrawLine3D(entity->origin, target, YELLOW);
    DrawSphere(target, 0.12f, YELLOW);
}

static void UpdateSelection(EditorState &editor)
{
    if (editor.looking || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !IsMouseOverViewport(editor)) return;

    // Pressing on a resize handle of the selected volume starts resizing it, once the mouse moves
    editor.dragMoved = false;
    editor.dragStartMouse = GetMousePosition();
    editor.resizeFace = HoveredResizeHandle(editor);
    if (editor.resizeFace >= 0)
    {
        editor.dragging = true;
        return;
    }

    Ray ray = MouseRay(editor);
    int closest = -1;
    float closestDistance = 0.0f;
    for (size_t i = 0; i < editor.level.entities.size(); i++)
    {
        const LevelEntity &entity = editor.level.entities[i];
        if (!IsEntityShown(entity)) continue;
        RayCollision hit = GetRayCollisionSphere(ray, entity.origin, PickRadius(entity));
        if (hit.hit && (closest < 0 || hit.distance < closestDistance))
        {
            closest = (int)i;
            closestDistance = hit.distance;
        }
    }
    editor.selected = closest;  // Clicking empty space deselects

    // Pressing on an entity can turn into dragging it, once the mouse moves
    editor.dragging = (closest >= 0);
}

void UpdateViewport(EditorState &editor)
{
    UpdateCamera(editor);
    if (editor.pointing)
    {
        UpdatePointAt(editor);  // Clicks aim instead of selecting while Point At is on
        return;
    }
    UpdateSelection(editor);
    UpdateDrag(editor);
}

// Draws an entity the way entitylist.txt describes it: a box or sphere in its color, plus an arrow for
// an angle or direction parameter, and a sphere showing any radius parameter
static void DrawEntityMarker(const LevelEntity &entity, bool selected)
{
    const Vector3 &origin = entity.origin;
    const EntityClassInfo *entityClass = FindEntityClass(entity.classname);
    if (entityClass == nullptr)
    {
        DrawCube(origin, 0.4f, 0.4f, 0.4f, MAGENTA);  // Not in entitylist.txt
        if (selected) DrawCubeWires(origin, 0.5f, 0.5f, 0.5f, WHITE);
        return;
    }

    Color color = entityClass->colorParam.empty() ? entityClass->color : EntityColorOrDefault(entity, entityClass->colorParam.c_str(), WHITE);

    if (entityClass->shape == "box")
    {
        BoundingBox box = { Vector3Add(origin, entityClass->mins), Vector3Add(origin, entityClass->maxs) };
        DrawBoundingBox(box, selected ? WHITE : color);
    }
    else if (entityClass->shape == "volume")
    {
        // Its own box, from its mins and maxs parameters, see-through so what's inside still shows. The
        // small cube at its origin is the handle for clicking and dragging it
        BoundingBox box = VolumeBox(entity);
        DrawCubeV(Vector3Scale(Vector3Add(box.min, box.max), 0.5f), Vector3Subtract(box.max, box.min), Fade(color, 0.15f));
        DrawBoundingBox(box, selected ? WHITE : color);
        DrawCube(origin, 0.25f, 0.25f, 0.25f, color);
        if (selected) DrawCubeWires(origin, 0.35f, 0.35f, 0.35f, WHITE);
    }
    else
    {
        DrawSphere(origin, entityClass->radius, color);
        if (selected) DrawSphereWires(origin, entityClass->radius*1.3f + 0.05f, 8, 8, WHITE);
    }

    for (const EntityParamInfo &param : entityClass->params)
    {
        if (param.type == "angle")
        {
            float angle = EntityNumberOrDefault(entity, param.name.c_str(), 0.0f)*DEG2RAD;
            Vector3 facing = { -sinf(angle), 0.0f, -cosf(angle) };
            DrawLine3D(origin, Vector3Add(origin, Vector3Scale(facing, 1.2f)), selected ? WHITE : color);
        }
        else if (param.type == "direction")
        {
            Vector3 direction = Vector3Normalize(EntityVectorOrDefault(entity, param.name.c_str(), { 0.0f, -1.0f, 0.0f }));
            DrawLine3D(origin, Vector3Add(origin, Vector3Scale(direction, 3.0f)), ORANGE);
        }
        else if (param.type == "radius")
        {
            float radius = EntityNumberOrDefault(entity, param.name.c_str(), 1.0f);
            DrawSphereWires(origin, radius, 8, 8, Fade(color, selected ? 0.6f : 0.25f));
        }
    }
}

// The selected volume's resize handles, drawn over everything so the ones inside walls and floors can still
// be grabbed. The one under the mouse, or being dragged, is highlighted
static void DrawResizeHandles(const EditorState &editor)
{
    if (editor.selected < 0 || editor.selected >= (int)editor.level.entities.size()) return;
    const LevelEntity &entity = editor.level.entities[editor.selected];
    if (!IsVolume(entity) || !IsEntityShown(entity)) return;

    int active = (editor.dragging && editor.resizeFace >= 0) ? editor.resizeFace
               : (!editor.dragging && !editor.looking && IsMouseOverViewport(editor)) ? HoveredResizeHandle(editor) : -1;
    BoundingBox box = VolumeBox(entity);

    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (int face = 0; face < 6; face++)
    {
        Vector3 position = HandlePosition(editor, box, face);
        float size = HandleSize(editor, position);
        DrawCube(position, size, size, size, (face == active) ? YELLOW : WHITE);
        DrawCubeWires(position, size, size, size, BLACK);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}

static void DrawEntityLabels(const EditorState &editor)
{
    const int fontSize = 16;
    Rectangle view = GetViewportRect();
    Vector3 forward = Vector3Subtract(editor.camera.target, editor.camera.position);

    for (size_t i = 0; i < editor.level.entities.size(); i++)
    {
        const LevelEntity &entity = editor.level.entities[i];
        if (!IsEntityShown(entity)) continue;

        // Skip entities behind the camera, which would otherwise get projected onto the screen mirrored
        if (Vector3DotProduct(Vector3Subtract(entity.origin, editor.camera.position), forward) <= 0.0f) continue;

        Vector2 screen = GetWorldToScreenEx(entity.origin, editor.camera, (int)view.width, (int)view.height);
        const char *label = entity.classname.c_str();
        int width = (int)MeasureUIText(label, (float)fontSize);
        int x = (int)(view.x + screen.x) - width/2;
        int y = (int)(view.y + screen.y) - fontSize - 18;

        bool selected = ((int)i == editor.selected);
        DrawRectangle(x - 4, y - 2, width + 8, fontSize + 4, Fade(BLACK, selected ? 0.85f : 0.55f));
        DrawUIText(label, (float)x, (float)y, (float)fontSize, selected ? WHITE : YELLOW);
    }
}

// The 3D view is rendered into this, at the viewport's size, so its center is the middle of the
// visible area rather than hidden behind the details panel
static RenderTexture2D viewTarget = { 0 };

void UnloadViewport()
{
    if (viewTarget.id != 0) UnloadRenderTexture(viewTarget);
    viewTarget = { 0 };
}

// Loads or unloads the sky when the level's skybox setting changes, like from World Settings or an undo
static void SyncSkybox(EditorState &editor)
{
    const std::string &wanted = editor.level.info.skybox;
    if (wanted == editor.skyboxTried) return;
    editor.skyboxTried = wanted;

    if (wanted.empty()) UnloadSkybox(editor.skybox);
    else if (!LoadSkybox(editor.skybox, wanted.c_str())) SetStatus(editor, "Couldn't load the skybox %s", wanted.c_str());
}

// The ground grid at the current grid size. It's centred under the camera so a fine grid is still
// around wherever you're working, moved in whole grid steps so its lines stay on the snapping points
static void DrawEditorGrid(const EditorState &editor)
{
    float size = editorSettings.gridSize;
    if (size <= 0.0f) return;

    // About 64 units across for the big sizes, capped at 256 lines for the small ones. An even count,
    // so there's a line through the centre
    int slices = (int)(64.0f/size);
    if (slices > 256) slices = 256;
    if (slices < 16) slices = 16;
    slices -= slices%2;

    rlPushMatrix();
        rlTranslatef(roundf(editor.camera.position.x/size)*size, 0.0f, roundf(editor.camera.position.z/size)*size);
        DrawGrid(slices, size);
    rlPopMatrix();
}

// Keeps the sun's bounce lightmap up to date. Whenever its key changes, because the sun moved, turned or
// changed color or intensity, or the world model was reloaded, a new bake starts in the background, once
// things have been still for a moment so dragging doesn't start one every frame. A level's saved lightmap
// is used instead when it still matches. The lightmap updates when a bake finishes
static void UpdateEditorBounce(EditorState &editor)
{
    static unsigned long long lastKey = 0;
    static unsigned long long triedLoading = 0;  // The key a saved lightmap was last tried for, so it's tried once
    static double changedAt = 0.0;

    World &world = editor.world;
    BounceStats stats;
    if (UpdateLightmapBake(world, &stats))
    {
        SetStatus(editor, "Baked the bounce lightmap (%dx%d, %d texels) in %.0f ms", stats.width, stats.height, stats.texels, stats.bakeMs);
    }

    unsigned long long key = LightmapKey(world);
    if (key != lastKey)
    {
        lastKey = key;
        changedAt = GetTime();
    }
    if (key == world.lightmapKey || key == LightmapBakingKey(world)) return;  // Up to date, or being baked

    // The lightmap saved with the level, if it was baked from this world and sun
    if (!editor.levelPath.empty() && key != triedLoading)
    {
        triedLoading = key;
        if (LoadLightmap(world, LightmapPath(editor.levelPath).c_str(), editor.level.info.lightmapKey))
        {
            SetStatus(editor, "Loaded the level's bounce lightmap");
            return;
        }
    }

    if (GetTime() - changedAt < 0.25) return;
    StartLightmapBake(world);  // Cancels one still running for an older key
}

// A prop_model as the world has it placed, to tell when the level's props change
struct PlacedProp
{
    std::string model;
    Vector3 origin;
    float angle, scale;
    bool isStatic;
    bool operator==(const PlacedProp &other) const
    {
        return model == other.model && Vector3Equals(origin, other.origin) && angle == other.angle && scale == other.scale && isStatic == other.isStatic;
    }
};

// Places the level's prop_models in the world whenever they change, so moving, turning or editing one shows
// right away. Their models load once each. Static props are part of the bounce bake, so once they've stayed
// put for a moment the lightmap is laid out again with them, which starts a new bake if they moved. They're
// placed even while hidden with the other point entities, so the lightmap matches the game's
static void SyncProps(EditorState &editor)
{
    static std::vector<PlacedProp> placed;
    static const Mesh *placedWorld = nullptr;  // The world they were placed in: a reload makes a new one
    static bool layoutWaiting = false;
    static double changedAt = 0.0;

    std::vector<PlacedProp> wanted;
    for (const LevelEntity &entity : editor.level.entities)
    {
        if (entity.classname != "prop_model") continue;
        wanted.push_back({ EntityTextOrDefault(entity, "model", ""), entity.origin, EntityNumberOrDefault(entity, "angle", 0.0f),
                           EntityNumberOrDefault(entity, "scale", 0.03125f), EntityBoolOrDefault(entity, "static", true) });
    }
    editor.world.propsHidden = !editorSettings.showPointEntities;

    bool newWorld = (editor.world.model.meshes != placedWorld);
    if (newWorld || wanted != placed)
    {
        bool hadStatic = false, hasStatic = false;
        for (const PlacedProp &prop : placed) hadStatic = hadStatic || prop.isStatic;
        for (const PlacedProp &prop : wanted) hasStatic = hasStatic || prop.isStatic;

        ClearProps(editor.world);
        for (const PlacedProp &prop : wanted)
        {
            if (prop.model.empty()) continue;
            int model = LoadPropModel(editor.world, prop.model);
            if (model >= 0) AddProp(editor.world, model, prop.origin, prop.angle, prop.scale, prop.isStatic, false);  // Not solid: the editor has no player to block
        }
        placed = wanted;
        placedWorld = editor.world.model.meshes;

        // A freshly loaded world is laid out with its props straight away, so its saved lightmap can match.
        // Otherwise it waits until the props stop moving, rather than every frame of a drag
        if (newWorld && hasStatic) BuildLightmapLayout(editor.world);
        else if (hadStatic || hasStatic)
        {
            layoutWaiting = true;
            changedAt = GetTime();
        }
    }

    if (layoutWaiting && GetTime() - changedAt > 0.3)
    {
        layoutWaiting = false;
        BuildLightmapLayout(editor.world);
    }
}

// Keeps the level's env_cubemaps captured. They're placed again when one is added, moved or resized, and
// captured again once nothing has changed for a moment after that, or after the sun, the lights or the
// bounce lightmap change, so what the editor reflects matches what the game captures when the level loads
static void SyncCubemaps(EditorState &editor)
{
    static std::vector<std::pair<Vector3, int>> placed;
    static const Mesh *placedWorld = nullptr;
    static unsigned long long lastLighting = 0;
    static double changedAt = 0.0;
    World &world = editor.world;

    std::vector<std::pair<Vector3, int>> wanted;
    for (const LevelEntity &entity : editor.level.entities)
    {
        if (entity.classname == "env_cubemap") wanted.push_back({ entity.origin, (int)Clamp(EntityNumberOrDefault(entity, "size", 128.0f), 16.0f, 1024.0f) });
    }
    bool same = (wanted.size() == placed.size()) && world.model.meshes == placedWorld;
    for (size_t i = 0; same && i < wanted.size(); i++) same = Vector3Equals(wanted[i].first, placed[i].first) && wanted[i].second == placed[i].second;
    if (!same)
    {
        ClearCubemaps(world);
        for (const auto &[position, size] : wanted) AddCubemap(world, position, size);
        placed = wanted;
        placedWorld = world.model.meshes;
        changedAt = GetTime();
    }
    if (world.cubemaps.empty()) return;

    // What the captures show: the sun, the lights and the bounce lightmap. A quick hash, to notice changes
    unsigned long long lighting = 14695981039346656037ull;
    auto hash = [&lighting](const void *data, size_t size)
    {
        for (size_t i = 0; i < size; i++) lighting = (lighting ^ ((const unsigned char *)data)[i])*1099511628211ull;
    };
    hash(&world.lighting.sun, sizeof(world.lighting.sun));
    for (const PointLight &light : world.lighting.lights) hash(&light, sizeof(light));
    hash(&world.lightmapKey, sizeof(world.lightmapKey));
    if (lighting != lastLighting)
    {
        lastLighting = lighting;
        changedAt = GetTime();
        world.cubemapsStale = true;
    }

    if (world.cubemapsStale && GetTime() - changedAt > 0.5) CaptureCubemaps(world);
}

void DrawViewport(EditorState &editor)
{
    Rectangle view = GetViewportRect();
    if (view.width < 1 || view.height < 1) return;

    // Remade when the window is resized
    if (viewTarget.texture.width != (int)view.width || viewTarget.texture.height != (int)view.height)
    {
        UnloadViewport();
        viewTarget = LoadRenderTexture((int)view.width, (int)view.height);
    }

    SyncSkybox(editor);

    if (editor.worldLoaded)
    {
        // Lights come from the level's entities every frame, so edits in the details panel show up right away
        ApplyLevelLights(editor.level, editor.world.lighting, editor.defaultSun, GetEntityListParamReader());
        SetLightmapSettings(editor.world, editor.level.info.luxelsPerUnit, editor.level.info.lightmapFiltered);  // From World Settings, or an undo
        SyncProps(editor);
        UpdateEditorBounce(editor);
        SyncCubemaps(editor);
        RenderWorldShadows(editor.world);
    }

    BeginTextureMode(viewTarget);
        ClearBackground(ViewportBackground());
        BeginMode3D(editor.camera);
            DrawSkybox(editor.skybox, editor.camera);  // First, so the world draws over it
            if (editor.worldLoaded) DrawWorld(editor.world, editorSettings.showColliders);
            if (editorSettings.showGrid) DrawEditorGrid(editor);
            for (size_t i = 0; i < editor.level.entities.size(); i++)
            {
                if (IsEntityShown(editor.level.entities[i])) DrawEntityMarker(editor.level.entities[i], (int)i == editor.selected);
            }
            DrawPointAtPreview(editor);
            DrawResizeHandles(editor);
        EndMode3D();
    EndTextureMode();

    // Render textures are stored upside down, hence the negative height
    DrawTextureRec(viewTarget.texture, { 0.0f, 0.0f, view.width, -view.height }, { view.x, view.y }, WHITE);

    if (editorSettings.showLabels) DrawEntityLabels(editor);

    if (editor.pointing)
    {
        const char *hint = "Point At: click a spot on the level. Hold right mouse to look around, Esc cancels";
        float width = MeasureUIText(hint, 18);
        float x = view.x + (view.width - width)/2;
        DrawRectangle((int)x - 10, (int)view.y + 8, (int)width + 20, 30, Fade(BLACK, 0.7f));
        DrawUIText(hint, x, view.y + 14, 18, YELLOW);
    }

    if (!editor.worldLoaded)
    {
        const char *hint = "No world model yet. Use Edit > Create World, or File > Open Level";
        DrawUIText(hint, view.x + (view.width - MeasureUIText(hint, 20))/2, view.y + 20, 20, UIMutedTextColor());
    }
}
