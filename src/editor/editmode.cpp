// Edit mode: the only place the level's geometry can be changed. Tools, after Scythe (Unreal) and Hammer:
//
//   Select (default)   click a mesh to select it, drag to move it (Alt: up and down), Ctrl+drag to drag out an
//                      instance of it, Shift+drag a face to resize the mesh that way, Shift+click a face to give
//                      it the material picked in the Materials tab. Ctrl+F flips the selected mesh's normals (inside
//                      out, for interiors). Shift+D deletes the face under the mouse, and the rest of its flat surface,
//                      for cleaning up. Delete deletes the selected mesh
//   Cube (B)           click, draw a rectangle on the surface, click, pull out its depth, click; Enter makes it
//   Rotate (R)         turns the selected mesh around its origin in 15 degree steps: left and right arrows
//                      around the vertical, up and down around the horizontal axis most across the view
//   Cut (C)            click a face, move to aim the cut, click; Enter cuts that face in two
//   Extrude (Ctrl+Shift)  drag a face out (or in), Blender style. P puts a pivot at the mouse: drag it to move
//                      it, then dragging a face swings it around the pivot instead, for curves. P again removes it
//   Push/Pull (Alt+Shift) drag a face out to extrude it, or in to cut its outline into the mesh, through it if far enough
//   Edge (E)           click edges to select them (Ctrl+click for more), drag one to move them (Alt: up and down),
//                      Ctrl+drag a selected one to extrude them into new faces
//   Bevel (B, with edges selected)  move the mouse right for the width, scroll for more steps; click or Enter applies
//
// Everything snaps to the grid. Drags redo their change from the geometry as it was when they started, every
// frame, and are one undo step. Leaving Edit mode joins faces split by the tools back into polygroups, and
// works out collision and the bounce lightmap; the level can only be saved then.

#include "editor.h"
#include "config.h"
#include "editinput.h"
#include "geotools.h"
#include "raygui.h"
#include "raymath.h"
#include "rlgl.h"
#include "style.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <set>

EditInputScript editInputScript;

static const float CLICK_PIXELS = 4.0f;       // Mouse movement that still counts as a click
static const float PICK_PIXELS = 10.0f;       // How close the mouse has to be to an edge or the pivot
static const char *DEFAULT_MATERIAL = "textures/dev/wall128.tga";  // Until one is picked in the Materials tab

static bool CtrlDown() { return EdKeyDown(KEY_LEFT_CONTROL) || EdKeyDown(KEY_RIGHT_CONTROL); }
static bool ShiftDown() { return EdKeyDown(KEY_LEFT_SHIFT) || EdKeyDown(KEY_RIGHT_SHIFT); }
static bool AltDown() { return EdKeyDown(KEY_LEFT_ALT) || EdKeyDown(KEY_RIGHT_ALT); }

// Where a ray meets a plane, if it does in front of it
static bool RayPlane(Ray ray, Vector3 planePoint, Vector3 planeNormal, Vector3 &hit)
{
    float facing = Vector3DotProduct(ray.direction, planeNormal);
    if (fabsf(facing) < 0.0001f) return false;
    float distance = Vector3DotProduct(Vector3Subtract(planePoint, ray.position), planeNormal)/facing;
    if (distance < 0.0f || distance > (float)VIEW_FAR) return false;
    hit = Vector3Add(ray.position, Vector3Scale(ray.direction, distance));
    return true;
}

static Vector3 SnapPoint(Vector3 p) { return { SnapToGrid(p.x), SnapToGrid(p.y), SnapToGrid(p.z) }; }

// Where a cut would go through a face: the point on the grid nearest the mouse's spot, kept on the face's plane
static Vector3 CutPoint(const GeoPick &pick)
{
    Vector3 snapped = SnapPoint(pick.point);
    return Vector3Add(snapped, Vector3Scale(pick.normal, Vector3DotProduct(Vector3Subtract(pick.point, snapped), pick.normal)));
}

static std::string CurrentMaterial()
{
    std::string material = GetPickedMaterial();
    return material.empty() ? DEFAULT_MATERIAL : material;
}

static bool ValidMesh(const EditorState &editor, int mesh) { return mesh >= 0 && mesh < (int)editor.level.geometry.meshes.size(); }

// Whether a pick is still a face of a mesh: after geometry changes, an old one may not be
static bool ValidPick(const EditorState &editor, const GeoPick &pick)
{
    if (!ValidMesh(editor, pick.mesh)) return false;
    const GeoMesh &mesh = editor.level.geometry.meshes[pick.mesh];
    return mesh.shape >= 0 && mesh.shape < (int)editor.level.geometry.shapes.size() && pick.face >= 0 &&
           pick.face < (int)editor.level.geometry.shapes[mesh.shape].faces.size();
}
static GeoShape &MeshShape(EditorState &editor, int mesh) { return editor.level.geometry.shapes[editor.level.geometry.meshes[mesh].shape]; }
static Matrix MeshTransform(const EditorState &editor, int mesh) { return GeoMeshTransform(editor.level.geometry.meshes[mesh]); }

// A world space direction or point in a mesh's own space
static Vector3 ToMeshDirection(const EditorState &editor, int mesh, Vector3 direction)
{
    return Vector3RotateByQuaternion(direction, QuaternionInvert(editor.level.geometry.meshes[mesh].rotation));
}
static Vector3 ToMeshPoint(const EditorState &editor, int mesh, Vector3 point)
{
    return Vector3Transform(point, MatrixInvert(MeshTransform(editor, mesh)));
}

// ---------------------------------------------------------------------------------------------
// Picking

GeoPick PickGeometry(const EditorState &editor, Ray ray)
{
    GeoPick best;
    float bestDistance = FLT_MAX;
    const Geometry &geometry = editor.level.geometry;
    for (size_t m = 0; m < geometry.meshes.size(); m++)
    {
        const GeoShape &shape = geometry.shapes[geometry.meshes[m].shape];
        Matrix transform = GeoMeshTransform(geometry.meshes[m]);
        for (size_t f = 0; f < shape.faces.size(); f++)
        {
            std::vector<Vector3> points = GeoFacePoints(shape, shape.faces[f], transform);
            if (points.size() < 3) continue;
            Vector3 normal = GeoPolygonNormal(points);
            if (Vector3DotProduct(normal, ray.direction) >= 0.0f) continue;  // Faces seen from behind
            std::vector<int> triangles = GeoTriangulate(points, normal);
            for (size_t t = 0; t + 2 < triangles.size(); t += 3)
            {
                RayCollision hit = GetRayCollisionTriangle(ray, points[triangles[t]], points[triangles[t + 1]], points[triangles[t + 2]]);
                if (!hit.hit || hit.distance >= bestDistance) continue;
                bestDistance = hit.distance;
                best.mesh = (int)m;
                best.face = (int)f;
                best.point = hit.point;
                best.normal = normal;
            }
        }
    }
    return best;
}

// A point's place on the screen, and whether it's in front of the camera
static Vector2 ToScreen(const EditorState &editor, Vector3 point, bool &inFront)
{
    Vector3 forward = Vector3Subtract(editor.camera.target, editor.camera.position);
    inFront = Vector3DotProduct(Vector3Subtract(point, editor.camera.position), forward) > 0.0f;
    Rectangle view = GetViewportRect();
    Vector2 screen = GetWorldToScreenEx(point, editor.camera, (int)view.width, (int)view.height);
    return { view.x + screen.x, view.y + screen.y };
}

static float DistanceToSegment(Vector2 p, Vector2 a, Vector2 b)
{
    Vector2 ab = Vector2Subtract(b, a);
    float lengthSq = Vector2LengthSqr(ab);
    float t = (lengthSq > 0.0f) ? Clamp(Vector2DotProduct(Vector2Subtract(p, a), ab)/lengthSq, 0.0f, 1.0f) : 0.0f;
    return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t)));
}

// The edge of the face under the mouse that's nearest the mouse on the screen, if it's close enough
static bool HoveredEdge(const EditorState &editor, GeoEdge &edge)
{
    const GeoPick &hover = editor.edit.hover;
    if (!ValidPick(editor, hover)) return false;
    const GeoShape &shape = editor.level.geometry.shapes[editor.level.geometry.meshes[hover.mesh].shape];
    const GeoFace &face = shape.faces[hover.face];
    Matrix transform = MeshTransform(editor, hover.mesh);
    float best = PICK_PIXELS;
    bool found = false;
    for (size_t k = 0; k < face.corners.size(); k++)
    {
        int a = face.corners[k], b = face.corners[(k + 1)%face.corners.size()];
        bool frontA, frontB;
        Vector2 sa = ToScreen(editor, Vector3Transform(shape.vertices[a], transform), frontA);
        Vector2 sb = ToScreen(editor, Vector3Transform(shape.vertices[b], transform), frontB);
        if (!frontA || !frontB) continue;
        float d = DistanceToSegment(EdMousePosition(), sa, sb);
        if (d < best)
        {
            best = d;
            edge = { hover.mesh, std::min(a, b), std::max(a, b) };
            found = true;
        }
    }
    return found;
}

static bool SameEdge(const GeoEdge &x, const GeoEdge &y) { return x.mesh == y.mesh && x.a == y.a && x.b == y.b; }

// ---------------------------------------------------------------------------------------------
// Changing the geometry

static void ShowGeometry(EditorState &editor)
{
    GeometryChanged(editor);
    SyncWorldWithLevel(editor);
}

// Starts a drag that changes the geometry. Until it ends, editor.edit.before is the geometry as it was
static void BeginDrag(EditorState &editor, EditState::Drag kind, const GeoPick &pick, Ray ray)
{
    EditState &e = editor.edit;
    e.drag = kind;
    e.dragMoved = false;
    e.dragStartMouse = EdMousePosition();
    e.before = editor.level.geometry;
    e.dragPick = pick;
    e.duplicated = false;
    e.dragVertical = AltDown();
    (void)ray;
}

// One undo step for the whole drag: the geometry from before it, then the result
static void CommitDrag(EditorState &editor, const std::string &description)
{
    EditState &e = editor.edit;
    e.drag = EditState::DRAG_NONE;
    if (!e.dragMoved) return;
    Geometry result = editor.level.geometry;
    editor.level.geometry = e.before;
    RecordUndo(editor, description);
    editor.level.geometry = result;
    ShowGeometry(editor);
}

static void CancelDrag(EditorState &editor)
{
    EditState &e = editor.edit;
    if (e.drag == EditState::DRAG_NONE) return;
    if (e.dragMoved) editor.level.geometry = e.before;
    e.drag = EditState::DRAG_NONE;
    ShowGeometry(editor);
}

// A plane for dragging along axis: through point, containing the axis, turned to face the camera
static Vector3 AxisPlaneNormal(const EditorState &editor, Vector3 point, Vector3 axis)
{
    Vector3 toCamera = Vector3Subtract(editor.camera.position, point);
    Vector3 normal = Vector3Subtract(toCamera, Vector3Scale(axis, Vector3DotProduct(toCamera, axis)));
    if (Vector3LengthSqr(normal) < 0.0001f) normal = (fabsf(axis.y) < 0.9f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    return Vector3Normalize(normal);
}

// For moving things: the floor plane through point, or with Alt a standing one facing the camera
static void StartMovePlane(EditorState &editor, Vector3 point, Ray ray)
{
    EditState &e = editor.edit;
    e.dragVertical = AltDown();
    e.planePoint = point;
    if (e.dragVertical)
    {
        Vector3 toCamera = Vector3Subtract(editor.camera.position, point);
        toCamera.y = 0.0f;
        e.planeNormal = (Vector3LengthSqr(toCamera) > 0.0001f) ? Vector3Normalize(toCamera) : Vector3{ 0.0f, 0.0f, 1.0f };
    }
    else e.planeNormal = { 0.0f, 1.0f, 0.0f };
    if (!RayPlane(ray, e.planePoint, e.planeNormal, e.dragStart)) e.dragStart = point;
}

// How far the mouse has moved things along the move plane since the drag started, in whole grid steps
static bool MoveOffset(EditorState &editor, Ray ray, Vector3 &offset)
{
    EditState &e = editor.edit;
    if (AltDown() != e.dragVertical)
    {
        // Switching between along the floor and up and down: start again from here, keeping what moved so far
        Vector3 hit;
        if (RayPlane(ray, e.planePoint, e.planeNormal, hit)) e.planePoint = Vector3Add(e.planePoint, Vector3Subtract(hit, e.dragStart));
        StartMovePlane(editor, e.planePoint, ray);
    }
    Vector3 hit;
    if (!RayPlane(ray, e.planePoint, e.planeNormal, hit)) return false;
    Vector3 moved = Vector3Add(Vector3Subtract(e.planePoint, e.dragPick.point), Vector3Subtract(hit, e.dragStart));
    if (e.dragVertical) offset = { 0.0f, SnapToGrid(moved.y), 0.0f };
    else offset = { SnapToGrid(moved.x), 0.0f, SnapToGrid(moved.z) };
    return true;
}

// How far along a face's normal the mouse has dragged, in whole grid steps
static bool NormalDistance(EditorState &editor, Ray ray, float &distance)
{
    Vector3 hit;
    if (!RayPlane(ray, editor.edit.planePoint, editor.edit.planeNormal, hit)) return false;
    distance = SnapToGrid(Vector3DotProduct(Vector3Subtract(hit, editor.edit.dragStart), editor.edit.dragPick.normal));
    return true;
}

static void StartNormalDrag(EditorState &editor, Ray ray)
{
    EditState &e = editor.edit;
    e.planePoint = e.dragPick.point;
    e.planeNormal = AxisPlaneNormal(editor, e.dragPick.point, e.dragPick.normal);
    if (!RayPlane(ray, e.planePoint, e.planeNormal, e.dragStart)) e.dragStart = e.dragPick.point;
}

// The faces of a mesh whose shape is the same as pick's, found again after a change, by where they are
static int FaceAt(const EditorState &editor, int mesh, Vector3 worldPoint, Vector3 normal)
{
    const GeoShape &shape = editor.level.geometry.shapes[editor.level.geometry.meshes[mesh].shape];
    Matrix transform = MeshTransform(editor, mesh);
    int best = -1;
    float bestDistance = 1.0f;
    for (size_t f = 0; f < shape.faces.size(); f++)
    {
        std::vector<Vector3> points = GeoFacePoints(shape, shape.faces[f], transform);
        Vector3 n = GeoPolygonNormal(points);
        if (Vector3DotProduct(n, normal) < 0.99f) continue;
        float d = fabsf(Vector3DotProduct(n, Vector3Subtract(worldPoint, points[0])));
        if (d < bestDistance) { bestDistance = d; best = (int)f; }
    }
    return best;
}

// Redoes a drag's change from the geometry it started with, for where the mouse is now
static void UpdateDragChange(EditorState &editor, Ray ray)
{
    EditState &e = editor.edit;
    Geometry &geometry = editor.level.geometry;
    geometry = e.before;
    int mesh = e.dragPick.mesh;

    switch (e.drag)
    {
        case EditState::DRAG_MOVE:
        {
            Vector3 offset;
            if (!MoveOffset(editor, ray, offset)) break;
            if (e.duplicated)
            {
                mesh = GeoDuplicateMesh(geometry, mesh);
                e.selectedMesh = mesh;
            }
            geometry.meshes[mesh].position = Vector3Add(e.before.meshes[e.dragPick.mesh].position, offset);
            break;
        }
        case EditState::DRAG_RESIZE:
        {
            float distance;
            if (NormalDistance(editor, ray, distance)) GeoMoveFace(MeshShape(editor, mesh), e.dragPick.face, distance);
            break;
        }
        case EditState::DRAG_EXTRUDE:
        {
            float distance;
            if (!NormalDistance(editor, ray, distance) || distance == 0.0f) break;
            if (e.pivotPlaced)
            {
                // Around the pivot: the drag's length along the face is the length of the arc, in 15 degree steps
                Vector3 center = e.dragPick.point;
                Vector3 toPivot = Vector3Subtract(e.pivot, center);
                Vector3 axis = Vector3CrossProduct(e.dragPick.normal, toPivot);
                if (Vector3LengthSqr(axis) < 0.0001f) axis = { 0.0f, 1.0f, 0.0f };
                axis = Vector3Normalize(axis);
                // The click point is anywhere on the face, so the pivot is rarely exactly level with it, which
                // tips the axis. Snap it to the world axis it's closest to, flattened into the face's plane so
                // the turn stays square to the face (on a face that's on the grid, that's the world axis itself)
                Vector3 worldAxis = { 0.0f, 0.0f, 0.0f };
                if (fabsf(axis.x) >= fabsf(axis.y) && fabsf(axis.x) >= fabsf(axis.z)) worldAxis.x = (axis.x < 0.0f) ? -1.0f : 1.0f;
                else if (fabsf(axis.y) >= fabsf(axis.z)) worldAxis.y = (axis.y < 0.0f) ? -1.0f : 1.0f;
                else worldAxis.z = (axis.z < 0.0f) ? -1.0f : 1.0f;
                worldAxis = Vector3Subtract(worldAxis, Vector3Scale(e.dragPick.normal, Vector3DotProduct(worldAxis, e.dragPick.normal)));
                if (Vector3LengthSqr(worldAxis) > 0.0001f) axis = Vector3Normalize(worldAxis);
                float radius = fmaxf(Vector3Length(Vector3Subtract(toPivot, Vector3Scale(axis, Vector3DotProduct(toPivot, axis)))), 1.0f);
                float degrees = roundf(fabsf(distance)/radius*RAD2DEG/15.0f)*15.0f;
                if (degrees <= 0.0f) break;
                int segments = (e.extrudeSegments > 0) ? e.extrudeSegments : std::max(1, (int)roundf(degrees/15.0f));
                GeoExtrudeFaceAround(MeshShape(editor, mesh), e.dragPick.face, ToMeshPoint(editor, mesh, e.pivot),
                                     ToMeshDirection(editor, mesh, axis), degrees, segments);
            }
            else GeoExtrudeFace(MeshShape(editor, mesh), e.dragPick.face, distance);
            break;
        }
        case EditState::DRAG_PUSHPULL:
        {
            float distance;
            if (NormalDistance(editor, ray, distance) && distance != 0.0f) GeoPushPull(geometry, mesh, e.dragPick.face, distance);
            break;
        }
        case EditState::DRAG_EDGES:
        {
            Vector3 offset;
            e.extrudedEdges = e.edges;
            if (!MoveOffset(editor, ray, offset)) break;
            if (e.duplicated)
            {
                // Ctrl: extrude them, each instanced shape once, through the first of its meshes picked
                if (Vector3LengthSqr(offset) == 0.0f) break;
                std::map<int, std::vector<size_t>> byShape;
                for (size_t i = 0; i < e.edges.size(); i++) byShape[geometry.meshes[e.edges[i].mesh].shape].push_back(i);
                for (auto &[shapeIndex, picked] : byShape)
                {
                    int edgeMesh = e.edges[picked[0]].mesh;
                    std::vector<std::pair<int, int>> pairs;
                    for (size_t i : picked) pairs.push_back({ e.edges[i].a, e.edges[i].b });
                    pairs = GeoExtrudeEdges(geometry.shapes[shapeIndex], pairs, ToMeshDirection(editor, edgeMesh, offset), geometry.meshes[edgeMesh].rotation);
                    for (size_t k = 0; k < picked.size(); k++) e.extrudedEdges[picked[k]] = { edgeMesh, std::min(pairs[k].first, pairs[k].second), std::max(pairs[k].first, pairs[k].second) };
                }
                break;
            }
            std::map<int, std::vector<int>> byMesh;
            for (const GeoEdge &edge : e.edges) { byMesh[edge.mesh].push_back(edge.a); byMesh[edge.mesh].push_back(edge.b); }
            std::set<int> shapesMoved;
            for (auto &[edgeMesh, vertices] : byMesh)
            {
                int shapeIndex = geometry.meshes[edgeMesh].shape;
                if (shapesMoved.count(shapeIndex)) continue;  // An instance of the same shape: it moves with it
                shapesMoved.insert(shapeIndex);
                GeoMoveVertices(geometry.shapes[shapeIndex], vertices, ToMeshDirection(editor, edgeMesh, offset));
            }
            break;
        }
        case EditState::DRAG_PIVOT:
        {
            Vector3 offset;
            if (MoveOffset(editor, ray, offset)) e.pivot = Vector3Add(e.dragPick.point, offset);
            break;
        }
        default: break;
    }
    ShowGeometry(editor);
}

static void EndDrag(EditorState &editor)
{
    EditState &e = editor.edit;
    EditState::Drag kind = e.drag;
    switch (e.drag)
    {
        case EditState::DRAG_MOVE: CommitDrag(editor, e.duplicated ? "Duplicate mesh" : "Move mesh"); break;
        case EditState::DRAG_RESIZE: CommitDrag(editor, "Resize face"); break;
        case EditState::DRAG_EXTRUDE: CommitDrag(editor, e.pivotPlaced ? "Extrude around pivot" : "Extrude face"); break;
        case EditState::DRAG_PUSHPULL: CommitDrag(editor, "Push/pull face"); break;
        case EditState::DRAG_EDGES:
            if (e.duplicated)
            {
                if (e.dragMoved) e.edges = e.extrudedEdges;  // Carry on from the new edges, so Ctrl+dragging again extrudes further
                CommitDrag(editor, "Extrude edges");
            }
            else CommitDrag(editor, "Move edges");
            break;
        case EditState::DRAG_PIVOT: e.drag = EditState::DRAG_NONE; break;
        default: e.drag = EditState::DRAG_NONE; break;
    }
    // Other changes can renumber a shape's vertices, which edges are picked by
    if (kind != EditState::DRAG_EDGES && kind != EditState::DRAG_PIVOT && kind != EditState::DRAG_MOVE) e.edges.clear();
}

// ---------------------------------------------------------------------------------------------
// One-off changes

static void ApplyMaterial(EditorState &editor, const GeoPick &pick)
{
    std::string material = CurrentMaterial();
    GeoShape &shape = MeshShape(editor, pick.mesh);
    GeoFace &face = shape.faces[pick.face];
    RecordUndo(editor, "Apply material");
    // World aligned, so it lines up with the faces around it that are too, like TrenchBroom
    face.material = material;
    GeoResetTexture(face, pick.normal);
    ShowGeometry(editor);
    SetStatus(editor, "Applied %s%s", material.c_str(), GetPickedMaterial().empty() ? " (pick one in the Materials tab to use another)" : "");
}

static void DeleteSelectedMesh(EditorState &editor)
{
    EditState &e = editor.edit;
    if (!ValidMesh(editor, e.selectedMesh)) return;
    RecordUndo(editor, "Delete mesh");
    GeoDeleteMesh(editor.level.geometry, e.selectedMesh);
    e.selectedMesh = -1;
    e.edges.clear();
    e.hover = GeoPick();  // Picked from the geometry as it was: the mesh may not be there any more
    e.cutStage = 0;
    ShowGeometry(editor);
    SetStatus(editor, "Deleted the mesh");
}

// Shift+D: the face under the mouse, and the faces joined to it on the same flat surface, gone. For cleaning up
// broken meshes. A mesh with no faces left goes too
static void DeleteHoveredFaces(EditorState &editor)
{
    EditState &e = editor.edit;
    if (!ValidPick(editor, e.hover))
    {
        SetStatus(editor, "Point at a face to delete it");
        return;
    }
    RecordUndo(editor, "Delete faces");
    int mesh = e.hover.mesh;
    int removed = GeoDeleteFaces(MeshShape(editor, mesh), e.hover.face);
    bool meshGone = MeshShape(editor, mesh).faces.empty();
    if (meshGone)
    {
        GeoDeleteMesh(editor.level.geometry, mesh);
        e.selectedMesh = -1;
    }
    e.hover = GeoPick();  // The face isn't there any more
    e.edges.clear();
    e.cutStage = 0;
    ShowGeometry(editor);
    if (meshGone) SetStatus(editor, "Deleted %d faces, the mesh's last", removed);
    else SetStatus(editor, "Deleted %d face%s. The mesh has a hole there now", removed, removed == 1 ? "" : "s");
}

// Ctrl+F: the selected mesh inside out, for interiors. Its shape changes, so every instance of it does too
static void FlipSelected(EditorState &editor)
{
    EditState &e = editor.edit;
    if (!ValidMesh(editor, e.selectedMesh))
    {
        SetStatus(editor, "Click a mesh to flip first");
        return;
    }
    int shape = editor.level.geometry.meshes[e.selectedMesh].shape;
    int instances = 0;
    for (const GeoMesh &mesh : editor.level.geometry.meshes) instances += (mesh.shape == shape) ? 1 : 0;

    RecordUndo(editor, "Flip normals");
    GeoFlipShape(MeshShape(editor, e.selectedMesh), editor.level.geometry.meshes[e.selectedMesh].rotation);
    e.edges.clear();
    ShowGeometry(editor);
    if (instances > 1) SetStatus(editor, "Flipped the mesh's normals, and its %d other instances'", instances - 1);
    else SetStatus(editor, "Flipped the mesh's normals: its faces look the other way now");
}

static void RotateSelected(EditorState &editor, Vector3 axis, float degrees)
{
    EditState &e = editor.edit;
    if (!ValidMesh(editor, e.selectedMesh))
    {
        SetStatus(editor, "Click a mesh to rotate first");
        return;
    }
    RecordUndo(editor, "Rotate mesh");
    GeoRotateMesh(editor.level.geometry, e.selectedMesh, axis, degrees);
    ShowGeometry(editor);
}

// The cube tool's box, from its corner, rectangle and depth
static void CreateCube(EditorState &editor)
{
    EditState &e = editor.edit;
    if (fabsf(e.cubeSizeU) < 0.5f || fabsf(e.cubeSizeV) < 0.5f || fabsf(e.cubeDepth) < 0.5f)
    {
        SetStatus(editor, "The cube needs a size in every direction");
        return;
    }
    RecordUndo(editor, "Create cube");
    e.selectedMesh = GeoAddBox(editor.level.geometry, e.cubeCorner, e.cubeU, e.cubeV, e.cubeNormal, e.cubeSizeU, e.cubeSizeV, e.cubeDepth, CurrentMaterial());
    e.cubeStage = 0;
    ShowGeometry(editor);
    SetStatus(editor, "Created a %g x %g x %g cube", fabsf(e.cubeSizeU), fabsf(e.cubeSizeV), fabsf(e.cubeDepth));
}

static void ApplyCut(EditorState &editor)
{
    EditState &e = editor.edit;
    const GeoPick &pick = e.cutPick;
    if (!ValidMesh(editor, pick.mesh)) return;
    Geometry before = editor.level.geometry;
    bool cut = GeoCutFace(MeshShape(editor, pick.mesh), pick.face, ToMeshPoint(editor, pick.mesh, pick.point), ToMeshDirection(editor, pick.mesh, e.cutDirection));
    if (!cut)
    {
        editor.level.geometry = before;
        SetStatus(editor, "The cut doesn't cross the face");
        return;
    }
    Geometry result = editor.level.geometry;
    editor.level.geometry = before;
    RecordUndo(editor, "Cut face");
    editor.level.geometry = result;
    e.cutStage = 0;
    ShowGeometry(editor);
    SetStatus(editor, "Cut the face in two");
}

// Bevel previews from the geometry as it was, and applies on a click or Enter
static void UpdateBevel(EditorState &editor, Ray ray)
{
    EditState &e = editor.edit;
    (void)ray;
    // Width: how far right the mouse has moved, in world units at the distance of the first edge
    const GeoEdge &first = e.edges[0];
    Vector3 point = Vector3Transform(e.before.shapes[e.before.meshes[first.mesh].shape].vertices[first.a], GeoMeshTransform(e.before.meshes[first.mesh]));
    float distance = Vector3Distance(editor.camera.position, point);
    float unitsPerPixel = 2.0f*distance*tanf(editor.camera.fovy*0.5f*DEG2RAD)/GetViewportRect().height;
    e.bevelWidth = fmaxf(0.0f, SnapToGrid((EdMousePosition().x - e.bevelStartMouse.x)*unitsPerPixel));
    float wheel = EdWheel();
    if (wheel != 0.0f) e.bevelSegments = (int)Clamp((float)e.bevelSegments + wheel, 1.0f, 16.0f);

    editor.level.geometry = e.before;
    std::map<int, std::vector<std::pair<int, int>>> byShape;
    for (const GeoEdge &edge : e.edges) byShape[e.before.meshes[edge.mesh].shape].push_back({ edge.a, edge.b });
    std::string error;
    for (auto &[shape, edges] : byShape)
    {
        if (!GeoBevelEdges(editor.level.geometry.shapes[shape], edges, e.bevelWidth, e.bevelSegments, error))
        {
            editor.level.geometry = e.before;
            SetStatus(editor, "Can't bevel: %s", error.c_str());
            break;
        }
    }
    ShowGeometry(editor);
}

static void StartBevel(EditorState &editor)
{
    EditState &e = editor.edit;
    if (e.edges.empty())
    {
        SetStatus(editor, "Select edges with the Edge tool (E) first");
        return;
    }
    e.tool = TOOL_BEVEL;
    e.beveling = true;
    e.before = editor.level.geometry;
    e.bevelStartMouse = EdMousePosition();
    e.bevelWidth = 0.0f;
    e.bevelSegments = 1;
    SetStatus(editor, "Bevel: move the mouse right for the width, scroll for more steps. Click or Enter applies, Esc cancels");
}

static void FinishBevel(EditorState &editor, bool apply)
{
    EditState &e = editor.edit;
    if (!e.beveling) return;
    e.beveling = false;
    if (apply && e.bevelWidth > 0.0f)
    {
        Geometry result = editor.level.geometry;
        editor.level.geometry = e.before;
        RecordUndo(editor, "Bevel edges");
        editor.level.geometry = result;
        SetStatus(editor, "Bevelled %d edges, %g wide in %d steps", (int)e.edges.size(), e.bevelWidth, e.bevelSegments);
    }
    else editor.level.geometry = e.before;
    e.edges.clear();  // Their vertices are gone
    e.tool = TOOL_EDGE;
    ShowGeometry(editor);
}

// ---------------------------------------------------------------------------------------------
// Modes and tools

void ResetEditTools(EditorState &editor)
{
    editor.edit = EditState();
}

static void CancelToolWork(EditorState &editor)
{
    EditState &e = editor.edit;
    CancelDrag(editor);
    FinishBevel(editor, false);
    e.cubeStage = 0;
    e.cutStage = 0;
}

static void SetTool(EditorState &editor, EditTool tool)
{
    CancelToolWork(editor);
    editor.edit.tool = tool;
    if (tool != TOOL_EXTRUDE && !editor.edit.pivotLocked) editor.edit.pivotPlaced = false;
}

void SetEditorMode(EditorState &editor, EditorMode mode)
{
    if (mode == editor.mode) return;
    if (mode == MODE_EDIT)
    {
        CancelPointAt(editor);
        editor.selected = -1;
        editor.dragging = false;
        editor.mode = MODE_EDIT;
        editor.edit.tool = TOOL_SELECT;
        SyncWorldWithLevel(editor);  // Quick builds from now on: no collision or lightmap until leaving
        SetStatus(editor, "Edit mode: the level's geometry. Tab goes back to Object mode");
        return;
    }

    CancelToolWork(editor);
    // Rounded the way the level file stores it, so what's built and baked now is exactly what's saved. Then faces
    // the tools split up are joined back where they're on one plane with one texture (polygroups)
    GeoQuantize(editor.level.geometry);
    for (GeoShape &shape : editor.level.geometry.shapes) GeoMergeFaces(shape);
    GeometryChanged(editor);
    editor.mode = MODE_OBJECT;
    editor.edit.edges.clear();
    double start = GetTime();
    SyncWorldWithLevel(editor);  // Collision and the lightmap layout: the bounce bake starts from this
    SetStatus(editor, "Object mode. Built the geometry's collision and lightmap layout in %.0f ms; the bounce bake runs in the background",
              (GetTime() - start)*1000.0);
}

void UpdateEditMode(EditorState &editor, bool keysFree, bool mouseFree)
{
    EditState &e = editor.edit;
    Ray ray = GetViewportMouseRay(editor);
    e.hover = mouseFree ? PickGeometry(editor, ray) : GeoPick();
    if (!ValidMesh(editor, e.selectedMesh)) e.selectedMesh = -1;

    // Keys
    if (keysFree && e.drag == EditState::DRAG_NONE)
    {
        bool ctrl = CtrlDown(), shift = ShiftDown(), alt = AltDown();
        bool plain = !ctrl && !shift && !alt;
        if (ctrl && shift && (EdKeyPressed(KEY_LEFT_SHIFT) || EdKeyPressed(KEY_RIGHT_SHIFT) || EdKeyPressed(KEY_LEFT_CONTROL) || EdKeyPressed(KEY_RIGHT_CONTROL)))
        {
            SetTool(editor, TOOL_EXTRUDE);
        }
        if (alt && shift && (EdKeyPressed(KEY_LEFT_SHIFT) || EdKeyPressed(KEY_RIGHT_SHIFT) || EdKeyPressed(KEY_LEFT_ALT) || EdKeyPressed(KEY_RIGHT_ALT)))
        {
            SetTool(editor, TOOL_PUSHPULL);
        }
        if (plain && EdKeyPressed(KEY_B))
        {
            if (!e.edges.empty()) StartBevel(editor);
            else SetTool(editor, TOOL_CUBE);
        }
        if (plain && EdKeyPressed(KEY_R)) SetTool(editor, TOOL_ROTATE);
        if (ctrl && !shift && !alt && EdKeyPressed(KEY_F)) FlipSelected(editor);
        if (shift && !ctrl && !alt && EdKeyPressed(KEY_D)) DeleteHoveredFaces(editor);
        if (plain && EdKeyPressed(KEY_C)) SetTool(editor, TOOL_CUT);
        if (plain && EdKeyPressed(KEY_E)) SetTool(editor, TOOL_EDGE);
        if (EdKeyPressed(KEY_DELETE) && e.tool != TOOL_EDGE) DeleteSelectedMesh(editor);

        if (e.tool == TOOL_EXTRUDE && plain && EdKeyPressed(KEY_P))
        {
            if (e.pivotPlaced)
            {
                e.pivotPlaced = e.pivotLocked = false;
                SetStatus(editor, "Pivot removed");
            }
            else
            {
                e.pivotPlaced = true;
                e.pivotLocked = false;
                e.pivot = SnapPoint(e.hover.mesh >= 0 ? e.hover.point : Vector3Add(ray.position, Vector3Scale(ray.direction, 256.0f)));
                SetStatus(editor, "Pivot placed. Drag it to move it; dragging a face now swings it around the pivot");
            }
        }
        if (e.tool == TOOL_ROTATE)
        {
            // Up and down turn around the horizontal axis most across the view
            Vector3 right = Vector3CrossProduct(Vector3Subtract(editor.camera.target, editor.camera.position), { 0.0f, 1.0f, 0.0f });
            Vector3 across = (fabsf(right.x) >= fabsf(right.z)) ? Vector3{ (right.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 0.0f, (right.z >= 0.0f) ? 1.0f : -1.0f };
            if (EdKeyPressed(KEY_LEFT)) RotateSelected(editor, { 0.0f, 1.0f, 0.0f }, 15.0f);
            if (EdKeyPressed(KEY_RIGHT)) RotateSelected(editor, { 0.0f, 1.0f, 0.0f }, -15.0f);
            if (EdKeyPressed(KEY_UP)) RotateSelected(editor, across, 15.0f);
            if (EdKeyPressed(KEY_DOWN)) RotateSelected(editor, across, -15.0f);
        }
        if (EdKeyPressed(KEY_ENTER) || EdKeyPressed(KEY_KP_ENTER))
        {
            if (e.tool == TOOL_CUBE && e.cubeStage >= 2) CreateCube(editor);
            else if (e.tool == TOOL_CUT && e.cutStage == 2) ApplyCut(editor);
            else if (e.tool == TOOL_BEVEL) FinishBevel(editor, true);
        }
    }

    if (e.tool == TOOL_BEVEL && e.beveling)
    {
        UpdateBevel(editor, ray);
        if (mouseFree && EdLeftPressed()) FinishBevel(editor, true);
        return;
    }

    // A drag in progress
    if (e.drag != EditState::DRAG_NONE)
    {
        if (!EdLeftDown() || editor.looking)
        {
            // Shift+click without moving: apply material
            if (e.drag == EditState::DRAG_RESIZE && !e.dragMoved)
            {
                e.drag = EditState::DRAG_NONE;
                ApplyMaterial(editor, e.dragPick);
                return;
            }
            // Ctrl+click on a selected edge without moving: deselect it
            if (e.drag == EditState::DRAG_EDGES && e.duplicated && !e.dragMoved)
            {
                e.drag = EditState::DRAG_NONE;
                auto found = std::find_if(e.edges.begin(), e.edges.end(), [&](const GeoEdge &x) { return SameEdge(x, e.pressedEdge); });
                if (found != e.edges.end()) e.edges.erase(found);
                return;
            }
            EndDrag(editor);
            return;
        }
        if (!e.dragMoved)
        {
            if (Vector2Distance(EdMousePosition(), e.dragStartMouse) < CLICK_PIXELS) return;
            e.dragMoved = true;
            if (e.drag == EditState::DRAG_MOVE || e.drag == EditState::DRAG_EDGES || e.drag == EditState::DRAG_PIVOT) StartMovePlane(editor, e.dragPick.point, ray);
            else StartNormalDrag(editor, ray);
        }
        UpdateDragChange(editor, ray);
        return;
    }

    if (!mouseFree) return;
    bool pressed = EdLeftPressed();
    const GeoPick &hover = e.hover;

    switch (e.tool)
    {
        case TOOL_SELECT:
        case TOOL_ROTATE:
        {
            if (!pressed) break;
            if (hover.mesh < 0)
            {
                e.selectedMesh = -1;
                break;
            }
            if (e.tool == TOOL_SELECT && ShiftDown() && !CtrlDown() && !AltDown())
            {
                BeginDrag(editor, EditState::DRAG_RESIZE, hover, ray);  // Or a click: apply material
                e.selectedMesh = hover.mesh;
                break;
            }
            e.selectedMesh = hover.mesh;
            if (e.tool == TOOL_SELECT)
            {
                BeginDrag(editor, EditState::DRAG_MOVE, hover, ray);
                e.duplicated = CtrlDown();
            }
            break;
        }
        case TOOL_EXTRUDE:
        {
            if (!pressed) break;
            // The pivot can be grabbed and moved until a face is extruded around it
            if (e.pivotPlaced && !e.pivotLocked)
            {
                bool inFront;
                Vector2 pivotScreen = ToScreen(editor, e.pivot, inFront);
                if (inFront && Vector2Distance(pivotScreen, EdMousePosition()) < PICK_PIXELS)
                {
                    GeoPick pivotPick;
                    pivotPick.point = e.pivot;
                    BeginDrag(editor, EditState::DRAG_PIVOT, pivotPick, ray);
                    break;
                }
            }
            if (hover.mesh < 0) break;
            e.selectedMesh = hover.mesh;
            if (e.pivotPlaced) e.pivotLocked = true;
            BeginDrag(editor, EditState::DRAG_EXTRUDE, hover, ray);
            break;
        }
        case TOOL_PUSHPULL:
        {
            if (!pressed || hover.mesh < 0) break;
            e.selectedMesh = hover.mesh;
            BeginDrag(editor, EditState::DRAG_PUSHPULL, hover, ray);
            break;
        }
        case TOOL_CUBE:
        {
            // Stage 0 follows the mouse; each click moves on: rectangle, depth, then Enter creates it
            Vector3 hit;
            if (e.cubeStage == 0)
            {
                if (!pressed) break;
                if (hover.mesh >= 0)
                {
                    // On the face: its plane, with the rectangle along the world axes across it
                    e.cubeNormal = hover.normal;
                    Vector3 a = { fabsf(hover.normal.x), fabsf(hover.normal.y), fabsf(hover.normal.z) };
                    if (a.y > 0.99f) { e.cubeNormal = { 0.0f, (hover.normal.y > 0) ? 1.0f : -1.0f, 0.0f }; e.cubeU = { 1, 0, 0 }; e.cubeV = { 0, 0, 1 }; }
                    else if (a.x > 0.99f) { e.cubeNormal = { (hover.normal.x > 0) ? 1.0f : -1.0f, 0.0f, 0.0f }; e.cubeU = { 0, 0, 1 }; e.cubeV = { 0, 1, 0 }; }
                    else if (a.z > 0.99f) { e.cubeNormal = { 0.0f, 0.0f, (hover.normal.z > 0) ? 1.0f : -1.0f }; e.cubeU = { 1, 0, 0 }; e.cubeV = { 0, 1, 0 }; }
                    else
                    {
                        Vector3 v;
                        GeoParaxialAxes(hover.normal, e.cubeU, v);
                        e.cubeU = Vector3Normalize(Vector3Subtract(e.cubeU, Vector3Scale(hover.normal, Vector3DotProduct(e.cubeU, hover.normal))));
                        e.cubeV = Vector3CrossProduct(hover.normal, e.cubeU);
                    }
                    e.cubeCorner = SnapPoint(hover.point);
                    // Keep the corner on the face's plane, even if snapping moved it off
                    e.cubeCorner = Vector3Add(e.cubeCorner, Vector3Scale(e.cubeNormal, Vector3DotProduct(Vector3Subtract(hover.point, e.cubeCorner), e.cubeNormal)));
                }
                else if (RayPlane(ray, { 0, 0, 0 }, { 0, 1, 0 }, hit))
                {
                    // Nothing under the mouse: the grid's floor
                    e.cubeNormal = { 0, 1, 0 };
                    e.cubeU = { 1, 0, 0 };
                    e.cubeV = { 0, 0, 1 };
                    e.cubeCorner = SnapPoint(hit);
                    e.cubeCorner.y = 0.0f;
                }
                else break;
                e.cubeSizeU = e.cubeSizeV = e.cubeDepth = 0.0f;
                e.cubeStage = 1;
            }
            else if (e.cubeStage == 1)
            {
                if (RayPlane(ray, e.cubeCorner, e.cubeNormal, hit))
                {
                    Vector3 d = Vector3Subtract(hit, e.cubeCorner);
                    e.cubeSizeU = SnapToGrid(Vector3DotProduct(d, e.cubeU));
                    e.cubeSizeV = SnapToGrid(Vector3DotProduct(d, e.cubeV));
                }
                if (pressed && e.cubeSizeU != 0.0f && e.cubeSizeV != 0.0f)
                {
                    e.cubeStage = 2;
                    e.planePoint = Vector3Add(e.cubeCorner, Vector3Add(Vector3Scale(e.cubeU, e.cubeSizeU*0.5f), Vector3Scale(e.cubeV, e.cubeSizeV*0.5f)));
                    e.planeNormal = AxisPlaneNormal(editor, e.planePoint, e.cubeNormal);
                    if (!RayPlane(ray, e.planePoint, e.planeNormal, e.dragStart)) e.dragStart = e.planePoint;
                }
            }
            else if (e.cubeStage == 2)
            {
                if (RayPlane(ray, e.planePoint, e.planeNormal, hit)) e.cubeDepth = SnapToGrid(Vector3DotProduct(Vector3Subtract(hit, e.dragStart), e.cubeNormal));
                if (pressed && e.cubeDepth != 0.0f) e.cubeStage = 3;
            }
            else if (e.cubeStage == 3 && pressed) CreateCube(editor);
            break;
        }
        case TOOL_CUT:
        {
            if (e.cutStage == 0)
            {
                if (!pressed || hover.mesh < 0) break;
                e.cutPick = hover;
                e.cutPick.point = CutPoint(hover);
                e.cutStage = 1;
            }
            else if (e.cutStage == 1)
            {
                Vector3 hit;
                if (RayPlane(ray, e.cutPick.point, e.cutPick.normal, hit) && Vector3Distance(hit, e.cutPick.point) > 0.5f)
                {
                    // Aimed in 15 degree steps from the face's texture direction, so straight cuts are easy
                    Vector3 u, v;
                    GeoParaxialAxes(e.cutPick.normal, u, v);
                    u = Vector3Normalize(Vector3Subtract(u, Vector3Scale(e.cutPick.normal, Vector3DotProduct(u, e.cutPick.normal))));
                    Vector3 w = Vector3CrossProduct(e.cutPick.normal, u);
                    Vector3 d = Vector3Subtract(hit, e.cutPick.point);
                    float angle = atan2f(Vector3DotProduct(d, w), Vector3DotProduct(d, u));
                    angle = roundf(angle/(15.0f*DEG2RAD))*15.0f*DEG2RAD;
                    e.cutDirection = Vector3Add(Vector3Scale(u, cosf(angle)), Vector3Scale(w, sinf(angle)));
                }
                if (pressed) e.cutStage = 2;
            }
            else if (pressed && hover.mesh >= 0)
            {
                // Clicking another face starts over there
                e.cutStage = 0;
            }
            break;
        }
        case TOOL_EDGE:
        {
            GeoEdge edge;
            bool overEdge = HoveredEdge(editor, edge);
            if (EdKeyPressed(KEY_DELETE)) e.edges.clear();
            if (!pressed) break;
            if (!overEdge)
            {
                if (!CtrlDown()) e.edges.clear();
                break;
            }
            auto found = std::find_if(e.edges.begin(), e.edges.end(), [&](const GeoEdge &x) { return SameEdge(x, edge); });
            if (CtrlDown() && found == e.edges.end())
            {
                e.edges.push_back(edge);
                break;
            }
            if (CtrlDown())
            {
                // On a selected edge: dragging extrudes the selected edges, a click deselects it
                e.pressedEdge = edge;
                e.selectedMesh = edge.mesh;
                BeginDrag(editor, EditState::DRAG_EDGES, hover, ray);
                e.duplicated = true;
                break;
            }
            if (found == e.edges.end()) e.edges = { edge };
            e.selectedMesh = edge.mesh;
            // Pressing on a selected edge can turn into dragging them
            GeoPick pick = hover;
            BeginDrag(editor, EditState::DRAG_EDGES, pick, ray);
            break;
        }
        default: break;
    }
}

bool EditModeEscape(EditorState &editor)
{
    EditState &e = editor.edit;
    if (e.drag != EditState::DRAG_NONE) { CancelDrag(editor); return true; }
    if (e.beveling) { FinishBevel(editor, false); return true; }
    if (e.cubeStage != 0 || e.cutStage != 0) { e.cubeStage = e.cutStage = 0; return true; }
    if (e.tool != TOOL_SELECT) { SetTool(editor, TOOL_SELECT); return true; }
    if (e.selectedMesh >= 0 || !e.edges.empty()) { e.selectedMesh = -1; e.edges.clear(); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Drawing

static void DrawPolygonOutline(const std::vector<Vector3> &points, Color color)
{
    for (size_t i = 0; i < points.size(); i++) DrawLine3D(points[i], points[(i + 1)%points.size()], color);
}

static void DrawPolygonFill(const std::vector<Vector3> &points, Color color)
{
    if (points.size() < 3) return;
    Vector3 normal = GeoPolygonNormal(points);
    std::vector<int> triangles = GeoTriangulate(points, normal);
    Vector3 lift = Vector3Scale(normal, 0.1f);
    for (size_t t = 0; t + 2 < triangles.size(); t += 3)
    {
        DrawTriangle3D(Vector3Add(points[triangles[t]], lift), Vector3Add(points[triangles[t + 1]], lift), Vector3Add(points[triangles[t + 2]], lift), color);
    }
}

void DrawEditModeOverlay(const EditorState &editor)
{
    if (editor.mode != MODE_EDIT) return;
    const EditState &e = editor.edit;
    const Geometry &geometry = editor.level.geometry;

    // Every face's outline, so the geometry's polygons show, then the selection over everything
    for (size_t m = 0; m < geometry.meshes.size(); m++)
    {
        const GeoShape &shape = geometry.shapes[geometry.meshes[m].shape];
        Matrix transform = GeoMeshTransform(geometry.meshes[m]);
        for (const GeoFace &face : shape.faces) DrawPolygonOutline(GeoFacePoints(shape, face, transform), Fade(BLACK, 0.5f));
    }
    rlDrawRenderBatchActive();
    rlDisableDepthTest();

    if (ValidPick(editor, e.hover) && e.tool != TOOL_CUBE && e.tool != TOOL_EDGE)
    {
        const GeoShape &shape = geometry.shapes[geometry.meshes[e.hover.mesh].shape];
        std::vector<Vector3> points = GeoFacePoints(shape, shape.faces[e.hover.face], MeshTransform(editor, e.hover.mesh));
        DrawPolygonOutline(points, SKYBLUE);
        rlEnableDepthTest();
        DrawPolygonFill(points, Fade(SKYBLUE, 0.2f));
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
    }

    if (ValidMesh(editor, e.selectedMesh))
    {
        const GeoShape &shape = geometry.shapes[geometry.meshes[e.selectedMesh].shape];
        Matrix transform = MeshTransform(editor, e.selectedMesh);
        for (const GeoFace &face : shape.faces) DrawPolygonOutline(GeoFacePoints(shape, face, transform), YELLOW);

        // The pivot it rotates around: its origin
        if (e.tool == TOOL_ROTATE)
        {
            Vector3 origin = geometry.meshes[e.selectedMesh].position;
            float size = Vector3Distance(editor.camera.position, origin)*0.06f;
            DrawLine3D(origin, Vector3Add(origin, { size, 0, 0 }), RED);
            DrawLine3D(origin, Vector3Add(origin, { 0, size, 0 }), GREEN);
            DrawLine3D(origin, Vector3Add(origin, { 0, 0, size }), BLUE);
            DrawSphere(origin, size*0.08f, WHITE);
        }
    }

    if (e.tool == TOOL_EDGE || e.tool == TOOL_BEVEL)
    {
        bool extruding = e.drag == EditState::DRAG_EDGES && e.duplicated && e.dragMoved;
        for (const GeoEdge &edge : extruding ? e.extrudedEdges : e.edges)
        {
            if (!ValidMesh(editor, edge.mesh)) continue;
            const GeoShape &shape = geometry.shapes[geometry.meshes[edge.mesh].shape];
            if (edge.a >= (int)shape.vertices.size() || edge.b >= (int)shape.vertices.size()) continue;
            Matrix transform = MeshTransform(editor, edge.mesh);
            Vector3 a = Vector3Transform(shape.vertices[edge.a], transform), b = Vector3Transform(shape.vertices[edge.b], transform);
            DrawLine3D(a, b, ORANGE);
            DrawCylinderEx(a, b, Vector3Distance(editor.camera.position, a)*0.002f, Vector3Distance(editor.camera.position, b)*0.002f, 4, ORANGE);
        }
        GeoEdge hovered;
        if (e.tool == TOOL_EDGE && e.drag == EditState::DRAG_NONE && HoveredEdge(editor, hovered))
        {
            const GeoShape &shape = geometry.shapes[geometry.meshes[hovered.mesh].shape];
            Matrix transform = MeshTransform(editor, hovered.mesh);
            Vector3 a = Vector3Transform(shape.vertices[hovered.a], transform), b = Vector3Transform(shape.vertices[hovered.b], transform);
            DrawCylinderEx(a, b, Vector3Distance(editor.camera.position, a)*0.002f, Vector3Distance(editor.camera.position, b)*0.002f, 4, WHITE);
        }
    }

    if (e.tool == TOOL_CUBE)
    {
        if (e.cubeStage == 0)
        {
            // Where a click would start: on the grid, on the surface under the mouse
            Vector3 point;
            bool found = false;
            if (e.hover.mesh >= 0) { point = SnapPoint(e.hover.point); found = true; }
            else
            {
                Vector3 hit;
                if (RayPlane(GetViewportMouseRay(editor), { 0, 0, 0 }, { 0, 1, 0 }, hit)) { point = SnapPoint(hit); point.y = 0; found = true; }
            }
            if (found)
            {
                float size = Vector3Distance(editor.camera.position, point)*0.02f;
                DrawLine3D(Vector3Subtract(point, { size, 0, 0 }), Vector3Add(point, { size, 0, 0 }), YELLOW);
                DrawLine3D(Vector3Subtract(point, { 0, size, 0 }), Vector3Add(point, { 0, size, 0 }), YELLOW);
                DrawLine3D(Vector3Subtract(point, { 0, 0, size }), Vector3Add(point, { 0, 0, size }), YELLOW);
            }
        }
        else
        {
            Vector3 corners[8];
            for (int i = 0; i < 8; i++)
            {
                corners[i] = Vector3Add(Vector3Add(Vector3Add(e.cubeCorner, Vector3Scale(e.cubeU, (i & 1) ? e.cubeSizeU : 0.0f)),
                                                   Vector3Scale(e.cubeV, (i & 2) ? e.cubeSizeV : 0.0f)), Vector3Scale(e.cubeNormal, (i & 4) ? e.cubeDepth : 0.0f));
            }
            const int lines[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (const auto &line : lines) DrawLine3D(corners[line[0]], corners[line[1]], YELLOW);
        }
    }

    // Before the first click, the dot follows the mouse over the meshes: where a click would put the cut
    if (e.tool == TOOL_CUT && e.cutStage == 0 && ValidPick(editor, e.hover))
    {
        Vector3 point = CutPoint(e.hover);
        DrawSphere(point, Vector3Distance(editor.camera.position, point)*0.01f, Fade(YELLOW, 0.8f));
    }

    if (e.tool == TOOL_CUT && e.cutStage > 0)
    {
        float size = Vector3Distance(editor.camera.position, e.cutPick.point)*0.01f;
        DrawSphere(e.cutPick.point, size, YELLOW);
        float length = 4096.0f;
        if (ValidMesh(editor, e.cutPick.mesh))
        {
            // As far as the face goes
            const GeoShape &shape = geometry.shapes[geometry.meshes[e.cutPick.mesh].shape];
            if (e.cutPick.face < (int)shape.faces.size())
            {
                length = 0.0f;
                for (Vector3 p : GeoFacePoints(shape, shape.faces[e.cutPick.face], MeshTransform(editor, e.cutPick.mesh))) length = fmaxf(length, Vector3Distance(p, e.cutPick.point));
            }
        }
        DrawLine3D(Vector3Subtract(e.cutPick.point, Vector3Scale(e.cutDirection, length)), Vector3Add(e.cutPick.point, Vector3Scale(e.cutDirection, length)), ORANGE);
    }

    if (e.tool == TOOL_EXTRUDE && e.pivotPlaced)
    {
        float size = Vector3Distance(editor.camera.position, e.pivot)*0.012f;
        DrawSphere(e.pivot, size, e.pivotLocked ? GRAY : MAGENTA);
        DrawLine3D(Vector3Subtract(e.pivot, { 0, size*6, 0 }), Vector3Add(e.pivot, { 0, size*6, 0 }), MAGENTA);
    }

    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}

// ---------------------------------------------------------------------------------------------
// The toolbar and view menu, over the 3D view

struct ToolButton
{
    EditTool tool;
    const char *label;
};
static const ToolButton TOOL_BUTTONS[] = {
    { TOOL_SELECT, "Select" }, { TOOL_CUBE, "Cube (B)" }, { TOOL_ROTATE, "Rotate (R)" }, { TOOL_CUT, "Cut (C)" },
    { TOOL_EXTRUDE, "Extrude (Ctrl+Shift)" }, { TOOL_PUSHPULL, "Push/Pull (Alt+Shift)" }, { TOOL_EDGE, "Edge (E)" }, { TOOL_BEVEL, "Bevel (B)" },
};

static const char *ToolHint(const EditorState &editor)
{
    const EditState &e = editor.edit;
    switch (e.tool)
    {
        case TOOL_SELECT: return "Click: select mesh | Drag: move (Alt: up/down) | Ctrl+drag: duplicate | Shift+drag face: resize | Shift+click face: apply material | Ctrl+F: flip normals | Shift+D: delete face | Del: delete";
        case TOOL_CUBE:
            if (e.cubeStage == 0) return "Cube: click on a surface or the grid to start";
            if (e.cubeStage == 1) return "Cube: move to draw the rectangle, click to lock it";
            if (e.cubeStage == 2) return "Cube: move to pull out the depth, click to lock it (Enter creates it now)";
            return "Cube: Enter or click creates it, Esc cancels";
        case TOOL_ROTATE: return "Rotate: click a mesh, then Left/Right turn it around the vertical, Up/Down around the horizontal, 15 degrees a press";
        case TOOL_CUT:
            if (e.cutStage == 0) return "Cut: click on a face where the cut goes through";
            if (e.cutStage == 1) return "Cut: move to aim it (15 degree steps), click to lock it";
            return "Cut: Enter cuts the face, Esc cancels";
        case TOOL_EXTRUDE: return e.pivotPlaced ? "Extrude around pivot: drag the pivot to move it, drag a face to swing it around | P: remove the pivot"
                                                : "Extrude: drag a face out or in | P: place a pivot at the mouse, to extrude around it";
        case TOOL_PUSHPULL: return "Push/Pull: drag a face out to extrude it, or in to cut its outline into the mesh (through it, if far enough)";
        case TOOL_EDGE: return "Edge: click edges (Ctrl+click: more), drag to move them (Alt: up/down), Ctrl+drag to extrude them | B: bevel the selected edges";
        case TOOL_BEVEL: return "Bevel: move the mouse right for the width, scroll for steps | Click or Enter applies, Esc cancels";
        default: return "";
    }
}

static Rectangle toolbarRect = { 0 };
static Rectangle viewMenuButton = { 0 };
static Rectangle gridLabel = { 0 };
static Rectangle viewMenuRect = { 0 };
static bool viewMenuOpen = false;

bool IsMouseOverViewportUI()
{
    Vector2 mouse = EdMousePosition();
    return CheckCollisionPointRec(mouse, toolbarRect) || CheckCollisionPointRec(mouse, viewMenuButton) || CheckCollisionPointRec(mouse, gridLabel) ||
           (viewMenuOpen && CheckCollisionPointRec(mouse, viewMenuRect));
}

void DrawViewportUI(EditorState &editor)
{
    Rectangle view = GetViewportRect();
    bool locked = editor.dialog != DIALOG_NONE || editor.openMenu != MENU_NONE;
    if (locked) GuiLock();

    // Modes, then (in Edit mode) the tools
    float x = view.x + 8, y = view.y + 8, height = 26;
    int mode = (editor.mode == MODE_EDIT) ? 1 : 0;
    int chosen = mode;
    GuiToggleGroup({ x, y, 96, height }, "Object;Edit (Tab)", &chosen);
    if (chosen != mode) SetEditorMode(editor, chosen == 1 ? MODE_EDIT : MODE_OBJECT);
    float right = x + 2*96 + 4;
    toolbarRect = { view.x + 4, view.y + 4, right - view.x, height + 8 };

    if (editor.mode == MODE_EDIT)
    {
        x = right + 12;
        for (const ToolButton &button : TOOL_BUTTONS)
        {
            float width = MeasureUIText(button.label, 16) + 18;
            bool active = (editor.edit.tool == button.tool);
            bool pressed = active;
            GuiToggle({ x, y, width, height }, button.label, &pressed);
            if (pressed != active && !active)
            {
                if (button.tool == TOOL_BEVEL) StartBevel(editor);
                else SetTool(editor, button.tool);
            }
            x += width + 4;
        }
        toolbarRect.width = x - toolbarRect.x;

        const char *hint = ToolHint(editor);
        float hintWidth = MeasureUIText(hint, 15);
        DrawRectangle((int)view.x + 8, (int)(y + height + 6), (int)hintWidth + 16, 22, Fade(BLACK, 0.6f));
        DrawUIText(hint, view.x + 16, y + height + 9, 15, RAYWHITE);
    }

    // The view menu: three dots stacked up, like most apps have, with the grid size next to it
    viewMenuButton = { view.x + view.width - 38, view.y + 8, 30, height };
    if (GuiButton(viewMenuButton, "")) viewMenuOpen = !viewMenuOpen;
    Color dots = GetColor(GuiGetStyle(BUTTON, TEXT_COLOR_NORMAL));
    for (int i = -1; i <= 1; i++)
    {
        DrawCircleV({ viewMenuButton.x + viewMenuButton.width/2, viewMenuButton.y + height/2 + i*6.0f }, 2.0f, dots);
    }

    const char *grid = editorSettings.snapToGrid ? TextFormat("Grid %g", editorSettings.gridSize) : TextFormat("Grid %g (no snap)", editorSettings.gridSize);
    float gridWidth = MeasureUIText(grid, 16) + 16;
    gridLabel = { viewMenuButton.x - gridWidth - 4, viewMenuButton.y, gridWidth, height };
    DrawRectangleRec(gridLabel, Fade(BLACK, 0.6f));
    DrawUIText(grid, gridLabel.x + 8, gridLabel.y + (height - 16)/2, 16, RAYWHITE);
    if (viewMenuOpen)
    {
        const char *labels[3] = { "Full 3D", "Shaded Unlit", "Wireframe" };
        viewMenuRect = { viewMenuButton.x + viewMenuButton.width - 160, viewMenuButton.y + height + 4, 160, 3*28.0f + 4 };
        DrawRectangleRec(viewMenuRect, GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
        DrawRectangleLinesEx(viewMenuRect, 1.0f, GetColor(GuiGetStyle(DEFAULT, LINE_COLOR)));
        for (int i = 0; i < 3; i++)
        {
            const char *label = TextFormat("%s %s", (editor.viewMode == (ViewMode)i) ? "*" : "  ", labels[i]);
            if (GuiButton({ viewMenuRect.x + 2, viewMenuRect.y + 2 + i*28.0f, viewMenuRect.width - 4, 26 }, label))
            {
                editor.viewMode = (ViewMode)i;
                viewMenuOpen = false;
            }
        }
        if (EdLeftPressed() && !CheckCollisionPointRec(EdMousePosition(), viewMenuRect) &&
            !CheckCollisionPointRec(EdMousePosition(), viewMenuButton)) viewMenuOpen = false;
    }

    if (locked) GuiUnlock();
}
