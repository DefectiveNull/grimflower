// Texturing mode: putting textures on the level's faces and lining them up, like Hammer's face edit mode.
//
//   Left click            select a face (Ctrl+click: add or take away more), clicking nothing deselects
//   Right click           put the texture picked in the Materials tab on the face, or on every selected face if
//                         it's one of them. Faces keep their alignment
//   Shift+right click     the same, and on across every face joined to it that had its old texture,
//                         autostitched: each one's alignment carries on from its neighbour's over the edge they
//                         share, wrapping round corners, so the whole surface tiles on from the clicked face
//   Shift+arrows          justify the texture on the selected faces (or the one under the mouse) up, down, left or
//                         right: its edge lines up with the face's
//   Shift+E               fit the texture to the face: once across each way
//
// Nothing here changes the geometry's shape, so faces stay the whole polygons (polygroups) Modelling mode left
// them as. Like Modelling mode, the world is rebuilt quickly after each change, without the collision, the
// lightmap layout and the bounce bake, which wait for Object mode.

#include "editor.h"
#include "editinput.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <tuple>

static bool CtrlDown() { return EdKeyDown(KEY_LEFT_CONTROL) || EdKeyDown(KEY_RIGHT_CONTROL); }
static bool ShiftDown() { return EdKeyDown(KEY_LEFT_SHIFT) || EdKeyDown(KEY_RIGHT_SHIFT); }
static bool AltDown() { return EdKeyDown(KEY_LEFT_ALT) || EdKeyDown(KEY_RIGHT_ALT); }

const char *TexturingHint()
{
    return "Click: select face (Ctrl: more) | Right click: apply texture | Shift+right click: apply and autostitch | "
           "Shift+arrows: justify | Shift+E: fit";
}

static bool ValidFace(const EditorState &editor, const GeoPick &pick)
{
    const Geometry &geometry = editor.level.geometry;
    if (pick.mesh < 0 || pick.mesh >= (int)geometry.meshes.size()) return false;
    int shape = geometry.meshes[pick.mesh].shape;
    return shape >= 0 && shape < (int)geometry.shapes.size() && pick.face >= 0 && pick.face < (int)geometry.shapes[shape].faces.size();
}

static bool SameFace(const GeoPick &a, const GeoPick &b) { return a.mesh == b.mesh && a.face == b.face; }

static GeoFace &FaceOf(EditorState &editor, const GeoPick &pick)
{
    return editor.level.geometry.shapes[editor.level.geometry.meshes[pick.mesh].shape].faces[pick.face];
}

// A face's corners in world space, where its texture axes are
static std::vector<Vector3> WorldPoints(const EditorState &editor, int mesh, int face)
{
    const Geometry &geometry = editor.level.geometry;
    const GeoShape &shape = geometry.shapes[geometry.meshes[mesh].shape];
    return GeoFacePoints(shape, shape.faces[face], GeoMeshTransform(geometry.meshes[mesh]));
}

// A texture's size in texels, from the world's cache (loading it there if the world hasn't yet)
static void TextureSize(EditorState &editor, const std::string &path, float &width, float &height)
{
    auto found = editor.world.textureCache.find(path);
    if (found == editor.world.textureCache.end())
    {
        Texture texture = { 0 };
        if (!LoadGameTexture(path.c_str(), texture)) texture = { 0 };
        found = editor.world.textureCache.emplace(path, texture).first;
    }
    width = (found->second.width > 0) ? (float)found->second.width : 64.0f;
    height = (found->second.height > 0) ? (float)found->second.height : 64.0f;
}

// An offset in texels, brought into one repeat of the texture: the same look, smaller numbers
static float Wrap(float offset, float size)
{
    float wrapped = fmodf(offset, size);
    if (wrapped < 0.0f) wrapped += size;
    if (size - wrapped < 0.001f) wrapped = 0.0f;
    return wrapped;
}

// The faces a change applies to: the selected ones, or with none selected the one under the mouse. Instances
// share their shape's faces, so each shape's face is only in once
static std::vector<GeoPick> TargetFaces(const EditorState &editor)
{
    const TextureState &t = editor.texturing;
    std::vector<GeoPick> faces = t.faces;
    if (faces.empty() && ValidFace(editor, t.hover)) faces.push_back(t.hover);

    std::vector<GeoPick> unique;
    std::set<std::pair<int, int>> seen;
    for (const GeoPick &pick : faces)
    {
        if (seen.insert({ editor.level.geometry.meshes[pick.mesh].shape, pick.face }).second) unique.push_back(pick);
    }
    return unique;
}

static void ShowChange(EditorState &editor)
{
    GeometryChanged(editor);
    SyncWorldWithLevel(editor);
}

// ---------------------------------------------------------------------------------------------
// Applying textures

static bool PickedTexture(EditorState &editor, std::string &material)
{
    material = GetPickedMaterial();
    if (material.empty()) SetStatus(editor, "Pick a texture in the Materials tab first");
    return !material.empty();
}

// Right click: the picked texture on the face, or on all the selected faces if it's one of them
static void ApplyTexture(EditorState &editor, const GeoPick &clicked)
{
    std::string material;
    if (!PickedTexture(editor, material)) return;
    const TextureState &t = editor.texturing;
    bool inSelection = false;
    for (const GeoPick &pick : t.faces) inSelection = inSelection || SameFace(pick, clicked);

    std::vector<GeoPick> faces = inSelection ? t.faces : std::vector<GeoPick>{ clicked };
    RecordUndo(editor, "Apply texture");
    std::set<std::pair<int, int>> done;
    for (const GeoPick &pick : faces)
    {
        if (!done.insert({ editor.level.geometry.meshes[pick.mesh].shape, pick.face }).second) continue;
        FaceOf(editor, pick).material = material;
    }
    ShowChange(editor);
    SetStatus(editor, "Applied %s to %d face%s", material.c_str(), (int)done.size(), done.size() == 1 ? "" : "s");
}

// Gives to the alignment that carries from's texture on over the edge from p to q, which they share: from's
// axes turned round the edge onto to's plane (the same, if they're on one plane), and the offset that makes
// both give the edge the same texture coordinates. texelsPerUnit is the level's texture density
static void Stitch(const GeoFace &from, Vector3 fromNormal, GeoFace &to, Vector3 toNormal, Vector3 p, Vector3 q, float width, float height,
                   float texelsPerUnit)
{
    Vector3 axis = Vector3Normalize(Vector3Subtract(q, p));
    float angle = atan2f(Vector3DotProduct(Vector3CrossProduct(fromNormal, toNormal), axis), Vector3DotProduct(fromNormal, toNormal));
    if (Vector3DotProduct(Vector3RotateByAxisAngle(fromNormal, axis, angle), toNormal) < 0.0f) angle = -angle;

    // Turned axes, with the rounding noise of the turn taken off so square corners stay exactly square
    auto turn = [&](Vector3 v)
    {
        Vector3 r = Vector3RotateByAxisAngle(v, axis, angle);
        for (float *c : { &r.x, &r.y, &r.z }) if (fabsf(*c) < 1e-5f) *c = 0.0f; else if (fabsf(fabsf(*c) - 1.0f) < 1e-5f) *c = (*c < 0.0f) ? -1.0f : 1.0f;
        return r;
    };
    to.uAxis = turn(from.uAxis);
    to.vAxis = turn(from.vAxis);
    to.scale = from.scale;
    // Along the edge, both faces' coordinates differ by the same amount (the turn keeps the edge's direction),
    // so matching them at p matches the whole edge
    to.offset.x = Wrap(from.offset.x + (Vector3DotProduct(p, from.uAxis) - Vector3DotProduct(p, to.uAxis))*texelsPerUnit/from.scale.x, width);
    to.offset.y = Wrap(from.offset.y + (Vector3DotProduct(p, from.vAxis) - Vector3DotProduct(p, to.vAxis))*texelsPerUnit/from.scale.y, height);
}

// A world space edge's key, the same whichever way round it's given, so faces of any mesh that share it find each other
typedef std::tuple<long long, long long, long long, long long, long long, long long> EdgeKey;
static EdgeKey KeyOf(Vector3 a, Vector3 b)
{
    auto snap = [](float v) { return (long long)llroundf(v*100.0f); };
    std::tuple<long long, long long, long long> ka = { snap(a.x), snap(a.y), snap(a.z) }, kb = { snap(b.x), snap(b.y), snap(b.z) };
    if (kb < ka) std::swap(ka, kb);
    return std::tuple_cat(ka, kb);
}

// A face waiting to be stitched from one next to it, over the edge from p to q. Gentler bends go first
struct StitchStep
{
    float bend;   // 1 - the dot product of the two faces' normals: 0 on one plane, 1 square, 2 folded back
    int order;    // Which was found first, between equal bends, so it comes out the same every time
    GeoPick from, to;
    Vector3 p, q;
    bool operator<(const StitchStep &other) const { return (bend != other.bend) ? bend > other.bend : order > other.order; }
};

// Shift+right click: the picked texture on the face, then on across the faces joined to it that had the same
// texture it had, each stitched from a face next to it that's already done. Always over the gentlest bend
// there is next, so a curve (the steps of an arch) wraps on from step to step, rather than every step folding
// off the flat face it touches
static void ApplyTextureStitched(EditorState &editor, const GeoPick &start)
{
    std::string material;
    if (!PickedTexture(editor, material)) return;
    Geometry &geometry = editor.level.geometry;
    std::string oldMaterial = FaceOf(editor, start).material;
    float width, height;
    TextureSize(editor, material, width, height);

    std::map<EdgeKey, std::vector<GeoPick>> byEdge;
    for (size_t m = 0; m < geometry.meshes.size(); m++)
    {
        const GeoShape &shape = geometry.shapes[geometry.meshes[m].shape];
        for (size_t f = 0; f < shape.faces.size(); f++)
        {
            std::vector<Vector3> points = WorldPoints(editor, (int)m, (int)f);
            GeoPick pick;
            pick.mesh = (int)m;
            pick.face = (int)f;
            for (size_t k = 0; k < points.size(); k++) byEdge[KeyOf(points[k], points[(k + 1)%points.size()])].push_back(pick);
        }
    }

    RecordUndo(editor, "Apply texture, autostitched");
    std::set<std::pair<int, int>> done;  // Shape and face, so an instance's copy isn't done twice
    auto isDone = [&](const GeoPick &pick) { return done.count({ geometry.meshes[pick.mesh].shape, pick.face }) > 0; };
    std::priority_queue<StitchStep> waiting;
    int found = 0;

    // A face is done: its neighbours that had the old texture wait their turn
    auto finish = [&](const GeoPick &at)
    {
        done.insert({ geometry.meshes[at.mesh].shape, at.face });
        std::vector<Vector3> points = WorldPoints(editor, at.mesh, at.face);
        Vector3 normal = GeoPolygonNormal(points);
        for (size_t k = 0; k < points.size(); k++)
        {
            Vector3 p = points[k], q = points[(k + 1)%points.size()];
            for (const GeoPick &other : byEdge[KeyOf(p, q)])
            {
                if (SameFace(other, at) || isDone(other) || FaceOf(editor, other).material != oldMaterial) continue;
                float bend = 1.0f - Vector3DotProduct(normal, GeoPolygonNormal(WorldPoints(editor, other.mesh, other.face)));
                if (bend > 1.99f) continue;  // Back to back, like two boxes touching: no way round
                waiting.push({ bend, found++, at, other, p, q });
            }
        }
    };

    FaceOf(editor, start).material = material;
    finish(start);
    while (!waiting.empty())
    {
        StitchStep step = waiting.top();
        waiting.pop();
        if (isDone(step.to)) continue;
        GeoFace &to = FaceOf(editor, step.to);
        Stitch(FaceOf(editor, step.from), GeoPolygonNormal(WorldPoints(editor, step.from.mesh, step.from.face)), to,
               GeoPolygonNormal(WorldPoints(editor, step.to.mesh, step.to.face)), step.p, step.q, width, height,
               (float)editor.level.info.texelsPerUnit);
        to.material = material;
        finish(step.to);
    }
    ShowChange(editor);
    SetStatus(editor, "Applied %s to %d face%s, autostitched from the one clicked", material.c_str(), (int)done.size(), done.size() == 1 ? "" : "s");
}

// ---------------------------------------------------------------------------------------------
// Alignment

enum Alignment { JUSTIFY_UP, JUSTIFY_DOWN, JUSTIFY_LEFT, JUSTIFY_RIGHT, FIT };

static void AlignFaces(EditorState &editor, Alignment how)
{
    std::vector<GeoPick> faces = TargetFaces(editor);
    if (faces.empty())
    {
        SetStatus(editor, "Select a face, or point at one, first");
        return;
    }
    static const char *names[] = { "Justify up", "Justify down", "Justify left", "Justify right", "Fit texture" };
    RecordUndo(editor, names[how]);
    for (const GeoPick &pick : faces)
    {
        GeoFace &face = FaceOf(editor, pick);
        float width, height;
        TextureSize(editor, face.material, width, height);

        // Where the face's corners are along the texture's axes, in texels at scale 1 (world units times the level's
        // texture density)
        float density = (float)editor.level.info.texelsPerUnit;
        float uMin = FLT_MAX, uMax = -FLT_MAX, vMin = FLT_MAX, vMax = -FLT_MAX;
        for (Vector3 p : WorldPoints(editor, pick.mesh, pick.face))
        {
            float u = Vector3DotProduct(p, face.uAxis)*density, v = Vector3DotProduct(p, face.vAxis)*density;
            uMin = fminf(uMin, u);
            uMax = fmaxf(uMax, u);
            vMin = fminf(vMin, v);
            vMax = fmaxf(vMax, v);
        }

        // In texels a corner is at u/scale + offset. The texture's top and left edge are at 0 (and every repeat
        // of it), its bottom and right edge at its height and width. v runs down the texture
        switch (how)
        {
            case JUSTIFY_UP: face.offset.y = Wrap(-vMin/face.scale.y, height); break;
            case JUSTIFY_DOWN: face.offset.y = Wrap(height - vMax/face.scale.y, height); break;
            case JUSTIFY_LEFT: face.offset.x = Wrap(-uMin/face.scale.x, width); break;
            case JUSTIFY_RIGHT: face.offset.x = Wrap(width - uMax/face.scale.x, width); break;
            case FIT:
                if (uMax - uMin > 0.001f)
                {
                    face.scale.x = (uMax - uMin)/width;
                    face.offset.x = Wrap(-uMin/face.scale.x, width);
                }
                if (vMax - vMin > 0.001f)
                {
                    face.scale.y = (vMax - vMin)/height;
                    face.offset.y = Wrap(-vMin/face.scale.y, height);
                }
                break;
        }
    }
    ShowChange(editor);
    SetStatus(editor, "%s: %d face%s", names[how], (int)faces.size(), faces.size() == 1 ? "" : "s");
}

// ---------------------------------------------------------------------------------------------
// Input

void UpdateTexturingMode(EditorState &editor, bool keysFree, bool mouseFree)
{
    TextureState &t = editor.texturing;
    // Undo can take faces away
    std::vector<GeoPick> valid;
    for (const GeoPick &pick : t.faces) if (ValidFace(editor, pick)) valid.push_back(pick);
    t.faces = valid;
    t.hover = mouseFree ? PickGeometry(editor, GetViewportMouseRay(editor)) : GeoPick();

    if (keysFree && ShiftDown() && !CtrlDown() && !AltDown())
    {
        if (EdKeyPressed(KEY_UP)) AlignFaces(editor, JUSTIFY_UP);
        if (EdKeyPressed(KEY_DOWN)) AlignFaces(editor, JUSTIFY_DOWN);
        if (EdKeyPressed(KEY_LEFT)) AlignFaces(editor, JUSTIFY_LEFT);
        if (EdKeyPressed(KEY_RIGHT)) AlignFaces(editor, JUSTIFY_RIGHT);
        if (EdKeyPressed(KEY_E)) AlignFaces(editor, FIT);
    }

    if (!mouseFree) return;
    if (EdLeftPressed())
    {
        if (!ValidFace(editor, t.hover))
        {
            if (!CtrlDown()) t.faces.clear();
        }
        else if (CtrlDown())
        {
            auto found = std::find_if(t.faces.begin(), t.faces.end(), [&](const GeoPick &x) { return SameFace(x, t.hover); });
            if (found != t.faces.end()) t.faces.erase(found);
            else t.faces.push_back(t.hover);
        }
        else t.faces = { t.hover };
    }
    if (editor.rightClicked && ValidFace(editor, t.hover))
    {
        if (ShiftDown()) ApplyTextureStitched(editor, t.hover);
        else ApplyTexture(editor, t.hover);
    }
}

bool TexturingModeEscape(EditorState &editor)
{
    if (editor.texturing.faces.empty()) return false;
    editor.texturing.faces.clear();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Drawing

static void DrawOutline(const std::vector<Vector3> &points, Color color)
{
    for (size_t i = 0; i < points.size(); i++) DrawLine3D(points[i], points[(i + 1)%points.size()], color);
}

// A see-through fill over a face, lifted off it a little so it doesn't fight the face for depth
static void DrawFill(const std::vector<Vector3> &points, Color color)
{
    if (points.size() < 3) return;
    Vector3 normal = GeoPolygonNormal(points);
    std::vector<int> triangles = GeoTriangulate(points, normal);
    Vector3 lift = Vector3Scale(normal, 0.1f);
    for (size_t k = 0; k + 2 < triangles.size(); k += 3)
    {
        DrawTriangle3D(Vector3Add(points[triangles[k]], lift), Vector3Add(points[triangles[k + 1]], lift), Vector3Add(points[triangles[k + 2]], lift), color);
    }
}

void DrawTexturingOverlay(const EditorState &editor)
{
    if (editor.mode != MODE_TEXTURING) return;
    const TextureState &t = editor.texturing;

    // Selected faces tinted, then their outlines and the one under the mouse over everything
    for (const GeoPick &pick : t.faces) if (ValidFace(editor, pick)) DrawFill(WorldPoints(editor, pick.mesh, pick.face), Fade(ORANGE, 0.25f));
    if (ValidFace(editor, t.hover)) DrawFill(WorldPoints(editor, t.hover.mesh, t.hover.face), Fade(SKYBLUE, 0.15f));
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (const GeoPick &pick : t.faces) if (ValidFace(editor, pick)) DrawOutline(WorldPoints(editor, pick.mesh, pick.face), ORANGE);
    if (ValidFace(editor, t.hover)) DrawOutline(WorldPoints(editor, t.hover.mesh, t.hover.face), SKYBLUE);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}
