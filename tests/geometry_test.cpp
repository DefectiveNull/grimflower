// Tests for level geometry: faces stay as they were built, polygroups, Push/Pull, collision and the level file.
// It drives the editor's real code in a hidden window. Run tests\run_tests.bat; -v shows raylib's log, which says
// which OpenGL it's using

#include "editor.h"
#include "bounce.h"
#include "editinput.h"
#include "entitylist.h"
#include "geotools.h"
#include "level.h"
#include "raymath.h"
#include <cstdio>
#include <map>
#include <set>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *MAT = "textures/dev/wall128.tga";

// A box. columns splits its top (y = 64) into that many quads along x
static GeoShape Box(int columns)
{
    GeoShape s;
    float x0 = -64, x1 = 64, y0 = 0, y1 = 64, z0 = -32, z1 = 32;
    auto face = [&](std::vector<int> c, Vector3 n) { GeoFace f; f.corners = c; f.material = MAT; GeoResetTexture(f, n); s.faces.push_back(f); };
    std::vector<int> frontTop, backTop;   // Top vertices at z1 and z0, left to right
    for (int i = 0; i <= columns; i++)
    {
        float x = x0 + (x1 - x0)*i/columns;
        frontTop.push_back((int)s.vertices.size()); s.vertices.push_back({ x, y1, z1 });
        backTop.push_back((int)s.vertices.size()); s.vertices.push_back({ x, y1, z0 });
    }
    int a = (int)s.vertices.size(); s.vertices.push_back({ x0, y0, z0 });
    int b = (int)s.vertices.size(); s.vertices.push_back({ x1, y0, z0 });
    int c = (int)s.vertices.size(); s.vertices.push_back({ x1, y0, z1 });
    int d = (int)s.vertices.size(); s.vertices.push_back({ x0, y0, z1 });
    for (int i = 0; i < columns; i++) face({ frontTop[i], frontTop[i + 1], backTop[i + 1], backTop[i] }, { 0, 1, 0 });
    face({ a, b, c, d }, { 0, -1, 0 });
    std::vector<int> front = { d, c }; for (int i = columns; i >= 0; i--) front.push_back(frontTop[i]);
    face(front, { 0, 0, 1 });
    std::vector<int> back = { b, a }; for (int i = 0; i <= columns; i++) back.push_back(backTop[i]);
    face(back, { 0, 0, -1 });
    face({ a, d, frontTop[0], backTop[0] }, { -1, 0, 0 });
    face({ c, b, backTop[columns], frontTop[columns] }, { 1, 0, 0 });
    return s;
}

static int FacesOnTop(const GeoShape &s)
{
    int n = 0;
    for (const GeoFace &f : s.faces)
    {
        bool top = true;
        for (int c : f.corners) top = top && s.vertices[c].y == 64.0f;
        n += top ? 1 : 0;
    }
    return n;
}

// Every edge is used once each way round
static int OpenEdges(const GeoShape &s)
{
    std::map<std::pair<int, int>, int> edges;
    for (const GeoFace &f : s.faces) for (size_t k = 0; k < f.corners.size(); k++) edges[{ f.corners[k], f.corners[(k + 1)%f.corners.size()] }]++;
    int open = 0;
    for (auto &[e, n] : edges) if (!edges.count({ e.second, e.first }) || n != 1) open++;
    return open;
}

static float Volume(const std::vector<std::vector<Vector3>> &piece)
{
    float v = 0;
    for (const std::vector<Vector3> &poly : piece)
        for (size_t i = 1; i + 1 < poly.size(); i++) v += Vector3DotProduct(poly[0], Vector3CrossProduct(poly[i], poly[i + 1]))/6.0f;
    return v;
}
static float ShapeVolume(const GeoShape &s)
{
    float total = 0;
    for (auto &piece : GeoConvexPieces(s)) total += fabsf(Volume(piece));
    return total;
}

static void Reset(EditorState &e, GeoShape shape)
{
    e.level.geometry = Geometry();
    e.level.geometry.shapes.push_back(shape);
    e.level.geometry.meshes.push_back({ 0, { 0, 0, 0 }, { 0, 0, 0, 1 } });
}

static GeoShape &S(EditorState &e) { return e.level.geometry.shapes[0]; }
static int FindTop(const GeoShape &s) { for (size_t f = 0; f < s.faces.size(); f++) { bool t = true; for (int c : s.faces[f].corners) t = t && s.vertices[c].y == 64.0f; if (t) return (int)f; } return -1; }

int main(int argc, char **argv)
{
    SetTraceLogLevel((argc > 1 && TextIsEqual(argv[1], "-v")) ? LOG_INFO : LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1600, 900, "geometry_test");
    std::string root = GetWorkingDirectory();  // run_tests.bat runs it from the project folder
    EditorState *ep = new EditorState();
    EditorState &e = *ep;
    InitEditor(e, root);
    editInputScript.active = true;

    printf("1 a cut across a flat face survives leaving Modelling mode\n");
    SetEditorMode(e, MODE_MODELLING);
    Reset(e, Box(1));
    CHECK(GeoCutFace(S(e), FindTop(S(e)), { 0, 64, 0 }, { 0, 0, 1 }), "cut failed");
    int faces = (int)S(e).faces.size();
    CHECK(FacesOnTop(S(e)) == 2, "top %d", FacesOnTop(S(e)));
    SetEditorMode(e, MODE_OBJECT);
    CHECK((int)S(e).faces.size() == faces, "faces %d -> %d", faces, (int)S(e).faces.size());
    CHECK(FacesOnTop(S(e)) == 2, "top merged: %d", FacesOnTop(S(e)));
    SetEditorMode(e, MODE_MODELLING);
    CHECK(FacesOnTop(S(e)) == 2, "top after coming back: %d", FacesOnTop(S(e)));
    SetEditorMode(e, MODE_OBJECT);

    printf("2 triangulation follows the user's edges\n");
    SetEditorMode(e, MODE_MODELLING);
    Reset(e, Box(3));
    SetEditorMode(e, MODE_OBJECT);
    {
        int expected = 0;
        for (const GeoFace &f : S(e).faces) expected += (int)f.corners.size() - 2;
        int tris = 0, crossing = 0;
        for (int m = 0; m < e.world.model.meshCount; m++)
        {
            const Mesh &mesh = e.world.model.meshes[m];
            tris += mesh.vertexCount/3;
            for (int t = 0; t < mesh.vertexCount/3; t++)
            {
                float *v = mesh.vertices + t*9;
                if (v[1] != 64 || v[4] != 64 || v[7] != 64) continue;
                float lo = fminf(v[0], fminf(v[3], v[6])), hi = fmaxf(v[0], fmaxf(v[3], v[6]));
                for (float edge : { -64.0f/3.0f, 64.0f/3.0f }) if (lo < edge - 0.5f && hi > edge + 0.5f) crossing++;
            }
        }
        CHECK(tris == expected, "tris %d expected %d", tris, expected);
        CHECK(crossing == 0, "%d triangles cross a user edge", crossing);
    }

    printf("3 polygroups: flat, same texture, connected\n");
    {
        GeoShape s = Box(3);
        GeoFinishShape(s);
        int g = -1; std::set<int> groups; int top = 0;
        for (const GeoFace &f : s.faces) { groups.insert(f.group); bool t = true; for (int c : f.corners) t = t && s.vertices[c].y == 64.0f; if (t) { if (g < 0) g = f.group; CHECK(f.group == g, "top split into groups"); top++; } }
        CHECK(top == 3, "top faces %d", top);
        CHECK(groups.size() == 6, "%d groups, want 6", (int)groups.size());
        GeoShape t = Box(3);
        t.faces[1].material = "textures/dev/other.tga";
        GeoFinishShape(t);
        CHECK(t.faces[0].group != t.faces[1].group && t.faces[0].group != t.faces[2].group, "different texture joined a polygroup");
        CHECK((int)t.faces.size() == (int)Box(3).faces.size(), "faces lost");
    }

    printf("4 Push/Pull keeps edges between other faces\n");
    {
        Geometry g; g.shapes.push_back(Box(3)); g.meshes.push_back({ 0, { 0, 0, 0 }, { 0, 0, 0, 1 } });
        float before = ShapeVolume(g.shapes[0]);
        GeoPushPull(g, 0, 2, -16.0f);
        int top = 0; for (const GeoFace &f : g.shapes[0].faces) { bool t = true; for (int c : f.corners) t = t && g.shapes[0].vertices[c].y == 64.0f; top += t; }
        CHECK(top == 2, "the two quads left on top should stay two, got %d", top);
        float after = ShapeVolume(g.shapes[0]);
        float want = before - (128.0f/3.0f)*64.0f*16.0f;
        CHECK(fabsf(after - want) < 1.0f, "volume %f want %f", after, want);
        CHECK(OpenEdges(g.shapes[0]) == 0, "open edges after the cut: %d", OpenEdges(g.shapes[0]));
    }

    printf("5 extruding one quad of a flat surface stays closed\n");
    {
        GeoShape s = Box(3);
        int open0 = OpenEdges(s);
        GeoExtrudeFace(s, 1, 32.0f);
        GeoFinishShape(s);
        CHECK(OpenEdges(s) == open0, "open edges %d", OpenEdges(s));
        CHECK(FacesOnTop(s) == 2, "untouched quads on the old top: %d", FacesOnTop(s));
    }

    printf("6 collision is the same with or without the edges\n");
    CHECK(fabsf(ShapeVolume(Box(1)) - ShapeVolume(Box(4))) < 1.0f, "%f vs %f", ShapeVolume(Box(1)), ShapeVolume(Box(4)));

    printf("7 level file: built faces and polygroups round trip, old files get them\n");
    {
        SetEditorMode(e, MODE_MODELLING);
        Reset(e, Box(3));
        SetEditorMode(e, MODE_OBJECT);
        const char *path = "build/tests/round.json";
        CHECK(SaveLevel(e.level, path), "save");
        Level loaded;
        CHECK(LoadLevel(loaded, path), "load");
        const GeoShape &a = S(e), &b = loaded.geometry.shapes[0];
        CHECK(a.faces.size() == b.faces.size(), "faces");
        for (size_t f = 0; f < a.faces.size(); f++) CHECK(a.faces[f].corners == b.faces[f].corners && a.faces[f].group == b.faces[f].group, "face %d differs", (int)f);
        char *text = LoadFileText(path);
        std::string s = text; UnloadFileText(text);
        CHECK(s.find("\"group\"") != std::string::npos, "no group in the file");
        size_t at;
        while ((at = s.find("\"group\"")) != std::string::npos) { size_t comma = s.rfind(",", at); size_t end = s.find_first_of("\r\n}", at); s.erase(comma, end - comma); }
        SaveFileText(path, s.c_str());
        Level old;
        bool ok = LoadLevel(old, path);
        CHECK(ok, "the file without groups should load");
        if (ok && !old.geometry.shapes.empty()) { for (size_t f = 0; f < a.faces.size(); f++) CHECK(old.geometry.shapes[0].faces[f].group == a.faces[f].group, "old file group %d", (int)f); }
    }

    printf("8 Texturing works on a polygroup whole\n");
    {
        SetEditorMode(e, MODE_MODELLING);
        Reset(e, Box(3));
        SetEditorMode(e, MODE_TEXTURING);
        int top = FindTop(S(e));
        GeoPick pick; pick.mesh = 0; pick.face = top; pick.point = { 0, 64, 0 }; pick.normal = { 0, 1, 0 };
        e.texturing.faces = { pick };
        editInputScript.keysDown = { KEY_LEFT_SHIFT };
        editInputScript.keysPressed = { KEY_E };
        UpdateTexturingMode(e, true, false);
        editInputScript.keysDown.clear(); editInputScript.keysPressed.clear();
        const GeoFace *first = nullptr; int n = 0;
        for (const GeoFace &f : S(e).faces)
        {
            bool t = true; for (int c : f.corners) t = t && S(e).vertices[c].y == 64.0f;
            if (!t) continue;
            if (!first) first = &f; else CHECK(Vector2Equals(first->offset, f.offset) && Vector2Equals(first->scale, f.scale), "a quad didn't line up");
            n++;
        }
        CHECK(n == 3, "top %d", n);
        CHECK(fabsf(first->scale.x - 0.5f) < 0.001f, "Fit used one quad, not the whole 128 wide surface on a 256 texture: scale %f", first->scale.x);
        SetEditorMode(e, MODE_OBJECT);
    }

    printf("9 out of date polygroups, as an undo can bring back, are redone on entering Texturing\n");
    {
        SetEditorMode(e, MODE_MODELLING);
        Reset(e, Box(3));
        SetEditorMode(e, MODE_OBJECT);
        for (GeoFace &f : S(e).faces) f.group = 0;
        SetEditorMode(e, MODE_TEXTURING);
        std::set<int> groups;
        for (const GeoFace &f : S(e).faces) groups.insert(f.group);
        CHECK(groups.size() == 6, "%d groups, want 6", (int)groups.size());
        SetEditorMode(e, MODE_OBJECT);
    }

    printf("10 every shipped level: finishing loses no faces\n");
    {
        FilePathList maps = LoadDirectoryFilesEx("maps", ".json", false);
        CHECK(maps.count > 0, "no levels found");
        for (unsigned i = 0; i < maps.count; i++)
        {
            Level level;
            bool ok = LoadLevel(level, maps.paths[i]);
            CHECK(ok, "couldn't load %s", maps.paths[i]);
            if (!ok) continue;
            int before = 0, after = 0, groups = 0;
            for (const GeoShape &loaded : level.geometry.shapes) for (const GeoFace &f : loaded.faces) CHECK(f.group >= 0, "no group straight after loading");
            for (GeoShape shape : level.geometry.shapes)
            {
                before += (int)shape.faces.size();
                GeoFinishShape(shape);
                after += (int)shape.faces.size();
                std::set<int> g; for (const GeoFace &f : shape.faces) g.insert(f.group);
                groups += (int)g.size();
                for (const GeoFace &f : shape.faces) CHECK(f.group >= 0, "ungrouped face");
            }
            printf("    %-28s faces %d -> %d, %d polygroups\n", GetFileName(maps.paths[i]), before, after, groups);
            CHECK(after <= before, "faces grew");
        }
        UnloadDirectoryFiles(maps);
    }

    printf("11 moving a corner folds off one triangle, and the rest stays as built\n");
    {
        GeoShape s = Box(2);                    // Vertices: 0, 2, 4 along the top front; 1, 3, 5 along the top back
        size_t before = s.faces.size();
        GeoMoveVertices(s, { 0 }, { 8.0f, 16.0f, 8.0f });  // Out of the plane of all three faces at that corner
        CHECK(OpenEdges(s) == 0, "open edges %d", OpenEdges(s));
        CHECK(s.faces.size() == before + 3, "%d faces, wanted three folds on %d", (int)s.faces.size(), (int)before);
        int bent = 0, pentagons = 0;
        for (const GeoFace &f : s.faces)
        {
            std::vector<Vector3> points; for (int c : f.corners) points.push_back(s.vertices[c]);
            Vector3 n = GeoPolygonNormal(points);
            for (Vector3 p : points) if (fabsf(Vector3DotProduct(n, Vector3Subtract(p, points[0]))) > 0.01f) { bent++; break; }
            pentagons += f.corners.size() == 5;
        }
        CHECK(bent == 0, "%d faces aren't flat", bent);
        CHECK(pentagons == 1, "the back face should be untouched: %d five cornered faces", pentagons);
    }

    printf("12 a vertex drag that makes a mesh concave splits its collision\n");
    {
        GeoShape s = Box(2);
        float signedVolume = 0;
        GeoMoveVertices(s, { 2, 3 }, { 0.0f, -32.0f, 0.0f });  // A valley across the middle of the top
        CHECK(OpenEdges(s) == 0, "open edges %d", OpenEdges(s));
        for (const GeoFace &f : s.faces)
        {
            std::vector<Vector3> points; for (int c : f.corners) points.push_back(s.vertices[c]);
            std::vector<int> triangles = GeoTriangulate(points, GeoPolygonNormal(points));
            for (size_t k = 0; k + 2 < triangles.size(); k += 3)
                signedVolume += Vector3DotProduct(points[triangles[k]], Vector3CrossProduct(points[triangles[k + 1]], points[triangles[k + 2]]))/6.0f;
        }
        std::vector<std::vector<std::vector<Vector3>>> pieces = GeoConvexPieces(s);
        CHECK(pieces.size() >= 2, "%d collision pieces for a concave mesh", (int)pieces.size());
        CHECK(fabsf(ShapeVolume(s) - signedVolume) < 1.0f, "collision volume %f, mesh volume %f", ShapeVolume(s), signedVolume);
        for (const auto &piece : pieces)
        {
            int outside = 0;
            for (const auto &face : piece)
            {
                Vector3 n = GeoPolygonNormal(face);
                for (const auto &other : piece) for (Vector3 p : other) if (Vector3DotProduct(n, Vector3Subtract(p, face[0])) > 0.05f) outside++;
            }
            CHECK(outside == 0, "a collision piece isn't convex");
        }

        SetEditorMode(e, MODE_MODELLING);
        Reset(e, s);
        SetEditorMode(e, MODE_OBJECT);
        CHECK(e.world.colliders.size() == pieces.size(), "%d colliders in the world, %d pieces", (int)e.world.colliders.size(), (int)pieces.size());
    }

    printf("13 the Vertex tool with scripted input: V, drag on the grid, Alt for height, undo\n");
    {
        SetEditorMode(e, MODE_MODELLING);
        Reset(e, Box(1));
        e.camera.position = { 300, 300, 300 };
        e.camera.target = { 0, 32, 0 };
        e.camera.up = { 0, 1, 0 };
        e.looking = false;
        const int corner = 2;  // The top front right corner, facing the camera
        Vector3 start = S(e).vertices[corner];

        auto frame = [&](Vector2 mouse, bool down, bool pressed, std::set<int> keys = {}, std::set<int> held = {})
        {
            editInputScript.mouse = mouse;
            editInputScript.leftDown = down;
            editInputScript.leftPressed = pressed;
            editInputScript.keysPressed = keys;
            editInputScript.keysDown = held;
            UpdateModellingMode(e, true, true);
        };
        auto screenOf = [&](Vector3 p)
        {
            Rectangle view = GetViewportRect();
            Vector2 s = GetWorldToScreenEx(p, e.camera, (int)view.width, (int)view.height);
            return Vector2{ view.x + s.x, view.y + s.y };
        };

        frame({ 0, 0 }, false, false, { KEY_V });
        CHECK(e.edit.tool == TOOL_VERTEX, "V didn't pick the tool: %d", (int)e.edit.tool);

        Vector2 at = screenOf(start);
        frame(at, false, false);
        frame(at, true, true);
        CHECK(e.edit.vertices.size() == 1 && e.edit.vertices[0].index == corner, "picked %d vertices, first %d", (int)e.edit.vertices.size(), e.edit.vertices.empty() ? -1 : e.edit.vertices[0].index);
        for (int i = 1; i <= 10; i++) frame({ at.x - i*12.0f, at.y + i*3.0f }, true, false);
        frame({ at.x - 120.0f, at.y + 30.0f }, false, false);
        Vector3 moved = S(e).vertices[corner];
        CHECK(moved.y == start.y, "a plain drag changed the height: %f", moved.y);
        CHECK(moved.x != start.x || moved.z != start.z, "a plain drag didn't move it");
        CHECK(SnapToGrid(moved.x) == moved.x && SnapToGrid(moved.z) == moved.z, "off the grid: %f %f", moved.x, moved.z);
        CHECK(OpenEdges(S(e)) == 0, "open edges %d", OpenEdges(S(e)));

        at = screenOf(moved);
        frame(at, false, false);
        frame(at, true, true, {}, { KEY_LEFT_ALT });
        for (int i = 1; i <= 10; i++) frame({ at.x, at.y - i*8.0f }, true, false, {}, { KEY_LEFT_ALT });
        frame({ at.x, at.y - 80.0f }, false, false);
        Vector3 raised = S(e).vertices[corner];
        CHECK(raised.x == moved.x && raised.z == moved.z, "Alt moved it sideways: %f %f", raised.x, raised.z);
        CHECK(raised.y > moved.y, "Alt didn't raise it: %f", raised.y);
        CHECK(SnapToGrid(raised.y) == raised.y, "height off the grid: %f", raised.y);

        Undo(e);
        CHECK(Vector3Equals(S(e).vertices[corner], moved), "undo should go back one drag");
        Undo(e);
        CHECK(Vector3Equals(S(e).vertices[corner], start), "undo should go back to the start");
        editInputScript.keysDown.clear(); editInputScript.keysPressed.clear();
        SetEditorMode(e, MODE_OBJECT);
    }

    printf("14 lock off: a tilted face keeps its texels world sized, and what stayed put keeps its texture\n");
    {
        GeoShape s = Box(1);
        GeoFace topBefore = s.faces[0];
        std::vector<Vector3> old = s.vertices;
        GeoMoveVertices(s, { 0 }, { 8.0f, 32.0f, 8.0f });
        int tilted = 0;
        for (const GeoFace &f : s.faces)
        {
            for (size_t k = 0; k < f.corners.size(); k++)
            {
                Vector3 a = s.vertices[f.corners[k]], b = s.vertices[f.corners[(k + 1)%f.corners.size()]];
                Vector2 ua = GeoTextureCoord(f, a, 64, 64, 1.0f), ub = GeoTextureCoord(f, b, 64, 64, 1.0f);
                float texels = Vector2Distance(ua, ub)*64.0f;
                CHECK(fabsf(texels - Vector3Distance(a, b)) < 0.05f, "stretched: edge %f long has %f texels", Vector3Distance(a, b), texels);
            }
            tilted += !Vector3Equals(f.uAxis, topBefore.uAxis) && !Vector3Equals(f.vAxis, topBefore.vAxis);
        }
        for (const GeoFace &f : s.faces)
        {
            std::vector<Vector3> points; for (int c : f.corners) points.push_back(s.vertices[c]);
            if (f.corners.size() != 3 || fabsf(GeoPolygonNormal(points).y) < 0.2f) continue;
            for (int c : f.corners)
            {
                if (c == 0) continue;
                Vector2 now = GeoTextureCoord(f, s.vertices[c], 64, 64, 1.0f), was = GeoTextureCoord(topBefore, old[c], 64, 64, 1.0f);
                CHECK(Vector2Distance(now, was) < 0.001f, "corner %d that didn't move slid on the texture", c);
            }
        }
        CHECK(tilted > 0, "no face's axes turned");

        GeoShape flat = Box(1);
        GeoFace wasTop = flat.faces[0];
        GeoMoveVertices(flat, { 0, 1 }, { 8.0f, 0.0f, 0.0f });  // Along the top's own plane
        CHECK(flat.faces[0].corners.size() == 4 && Vector3Equals(flat.faces[0].uAxis, wasTop.uAxis) && Vector2Equals(flat.faces[0].offset, wasTop.offset), "a move along the plane should leave the world alignment alone");
    }

    printf("15 lock on: corners keep their texture, through folds, and the world uses it\n");
    {
        GeoShape s = Box(1);
        Matrix identity = MatrixIdentity();
        std::map<int, Vector2> topUVs;
        for (GeoFace &f : s.faces) GeoLockTexture(s, f, identity, 1.0f);
        for (size_t k = 0; k < s.faces[0].corners.size(); k++) topUVs[s.faces[0].corners[k]] = s.faces[0].lockedUVs[k];
        GeoTextureMove locked;
        locked.locked = true;
        GeoMoveVertices(s, { 0 }, { 8.0f, 32.0f, 8.0f }, locked);
        int folded = 0;
        for (const GeoFace &f : s.faces)
        {
            CHECK(f.lockedUVs.size() == f.corners.size(), "a piece lost its UVs");
            bool top = true;
            for (int c : f.corners) top = top && topUVs.count(c);
            if (!top) continue;
            folded++;
            for (size_t k = 0; k < f.corners.size(); k++) CHECK(Vector2Distance(f.lockedUVs[k], topUVs[f.corners[k]]) < 0.0001f, "corner %d lost its UV", f.corners[k]);
        }
        CHECK(folded == 2, "the top should be two triangles, got %d", folded);

        GeoShape q = Box(1);
        for (GeoFace &f : q.faces) GeoLockTexture(q, f, identity, 1.0f);
        std::vector<Vector2> uvs = q.faces[0].lockedUVs;
        GeoMoveVertices(q, { 0 }, { 8.0f, 0.0f, 0.0f }, locked);  // In its plane: still a quad, now a stretched one
        CHECK(q.faces[0].corners.size() == 4, "the quad changed");
        CHECK(Vector2Equals(q.faces[0].lockedUVs[0], uvs[0]), "the moved corner lost its UV, so it wouldn't stretch");

        GeoMoveVertices(q, { 0 }, { 8.0f, 0.0f, 0.0f });  // Lock off again: back to the world
        CHECK(q.faces[0].lockedUVs.empty(), "lock off should free a moved face");
    }

    printf("16 the button: off by default, and a drag with it on stretches\n");
    {
        SetEditorMode(e, MODE_MODELLING);
        Reset(e, Box(1));
        e.camera.position = { 300, 300, 300 };
        e.camera.target = { 0, 32, 0 };
        e.camera.up = { 0, 1, 0 };
        CHECK(!e.edit.textureLock, "texture lock should start off");
        auto frame = [&](Vector2 mouse, bool down, bool pressed, std::set<int> keys = {})
        {
            editInputScript.mouse = mouse; editInputScript.leftDown = down; editInputScript.leftPressed = pressed;
            editInputScript.keysPressed = keys; editInputScript.keysDown.clear();
            UpdateModellingMode(e, true, true);
        };
        auto drag = [&](Vector3 from)
        {
            Rectangle view = GetViewportRect();
            Vector2 s = GetWorldToScreenEx(from, e.camera, (int)view.width, (int)view.height);
            Vector2 at = { view.x + s.x, view.y + s.y };
            frame(at, false, false);
            frame(at, true, true);
            for (int i = 1; i <= 10; i++) frame({ at.x - i*12.0f, at.y + i*3.0f }, true, false);
            frame({ at.x - 120.0f, at.y + 30.0f }, false, false);
        };
        auto lockedFaces = [&]() { int n = 0; for (const GeoFace &f : S(e).faces) n += !f.lockedUVs.empty(); return n; };

        frame({ 0, 0 }, false, false, { KEY_V });
        drag(S(e).vertices[2]);
        CHECK(!Vector3Equals(S(e).vertices[2], Box(1).vertices[2]), "the vertex didn't move");
        CHECK(lockedFaces() == 0, "with the lock off, %d faces got locked", lockedFaces());

        Reset(e, Box(1));
        e.edit.vertices.clear();
        ToggleTextureLock(e);
        CHECK(e.edit.textureLock, "the toggle");
        drag(S(e).vertices[2]);
        CHECK(!Vector3Equals(S(e).vertices[2], Box(1).vertices[2]), "the vertex didn't move with the lock on");
        CHECK(lockedFaces() == (int)S(e).faces.size() - 3, "the bottom, back and left don't touch the corner and shouldn't lock: %d of %d locked", lockedFaces(), (int)S(e).faces.size());

        printf("17 locked faces in the world, the level file and polygroups\n");
        SetEditorMode(e, MODE_OBJECT);
        const char *path = "build/tests/locked.json";
        CHECK(SaveLevel(e.level, path), "save");
        Level loaded;
        CHECK(LoadLevel(loaded, path), "load");
        int locked = 0;
        for (size_t f = 0; f < S(e).faces.size(); f++)
        {
            const GeoFace &a = S(e).faces[f], &b = loaded.geometry.shapes[0].faces[f];
            CHECK(a.lockedUVs.size() == b.lockedUVs.size(), "face %d's UVs didn't load", (int)f);
            for (size_t k = 0; k < a.lockedUVs.size(); k++) CHECK(Vector2Distance(a.lockedUVs[k], b.lockedUVs[k]) < 0.001f, "face %d UV %d", (int)f, (int)k);
            locked += !a.lockedUVs.empty();
        }
        CHECK(locked == lockedFaces(), "%d locked faces loaded", locked);

        // What's drawn is the locked coordinates
        for (const GeoFace &f : S(e).faces)
        {
            for (Vector2 uv : f.lockedUVs)
            {
                bool drawn = false;
                for (int m = 0; m < e.world.model.meshCount; m++)
                {
                    const Mesh &mesh = e.world.model.meshes[m];
                    for (int v = 0; v < mesh.vertexCount && !drawn; v++) drawn = fabsf(mesh.texcoords[v*2] - uv.x) < 0.001f && fabsf(mesh.texcoords[v*2 + 1] - uv.y) < 0.001f;
                }
                CHECK(drawn, "UV %f %f isn't in the world mesh", uv.x, uv.y);
            }
        }

        GeoShape grouped = Box(3);
        GeoLockTexture(grouped, grouped.faces[1], MatrixIdentity(), 1.0f);
        GeoBuildPolygroups(grouped);
        CHECK(grouped.faces[0].group != grouped.faces[1].group && grouped.faces[1].group != grouped.faces[2].group, "a locked face joined a polygroup");

        printf("18 aligning a locked face in Texturing mode frees it\n");
        SetEditorMode(e, MODE_TEXTURING);
        int lockedFace = -1;
        for (size_t f = 0; f < S(e).faces.size(); f++) if (!S(e).faces[f].lockedUVs.empty()) lockedFace = (int)f;
        GeoPick pick; pick.mesh = 0; pick.face = lockedFace;
        e.texturing.faces = { pick };
        editInputScript.keysDown = { KEY_LEFT_SHIFT };
        editInputScript.keysPressed = { KEY_E };
        UpdateTexturingMode(e, true, false);
        editInputScript.keysDown.clear(); editInputScript.keysPressed.clear();
        CHECK(S(e).faces[lockedFace].lockedUVs.empty(), "Fit should free it");
        SetEditorMode(e, MODE_OBJECT);

        printf("19 with the lock on, moving a mesh doesn't scroll its textures\n");
        SetEditorMode(e, MODE_MODELLING);
        Reset(e, Box(1));
        e.edit.vertices.clear();
        frame({ 0, 0 }, false, false, { KEY_ESCAPE });
        e.edit.tool = TOOL_SELECT;
        Rectangle view = GetViewportRect();
        Vector2 s = GetWorldToScreenEx({ 0, 64, 0 }, e.camera, (int)view.width, (int)view.height);
        Vector2 at = { view.x + s.x, view.y + s.y };
        frame(at, false, false);
        frame(at, true, true);
        for (int i = 1; i <= 10; i++) frame({ at.x - i*12.0f, at.y + i*3.0f }, true, false);
        frame({ at.x - 120.0f, at.y + 30.0f }, false, false);
        CHECK(!Vector3Equals(e.level.geometry.meshes[0].position, { 0, 0, 0 }), "the mesh didn't move");
        CHECK(lockedFaces() == (int)S(e).faces.size(), "every face should lock, got %d", lockedFaces());
        SetEditorMode(e, MODE_OBJECT);
        ToggleTextureLock(e);
    }

    printf("20 other tools can't leave UVs that don't match\n");
    {
        GeoShape s = Box(1);
        for (GeoFace &f : s.faces) GeoLockTexture(s, f, MatrixIdentity(), 1.0f);
        std::vector<Vector2> before = s.faces[0].lockedUVs;
        GeoFlipShape(s, { 0, 0, 0, 1 });
        CHECK(s.faces[0].lockedUVs.front().x == before.back().x, "flipping should keep each corner's UV");
        GeoFlipShape(s, { 0, 0, 0, 1 });
        GeoCutFace(s, 0, { 0, 64, 0 }, { 0, 0, 1 });
        GeoExtrudeFace(s, 1, 16.0f);
        Geometry g; g.shapes.push_back(s); g.meshes.push_back({ 0, { 0, 0, 0 }, { 0, 0, 0, 1 } });
        GeoPushPull(g, 0, 2, -8.0f);
        GeoFinishShape(g.shapes[0]);
        for (const GeoFace &f : g.shapes[0].faces) CHECK(f.lockedUVs.empty() || f.lockedUVs.size() == f.corners.size(), "%d UVs on %d corners", (int)f.lockedUVs.size(), (int)f.corners.size());
    }

    printf("21 prop_model's up and collision\n");
    {
        CHECK(LoadEntityList("entitylist.txt"), "entitylist.txt is missing");
        CHECK(FindEntityClass("prop_model") && FindEntityParam(*FindEntityClass("prop_model"), "collision") && FindEntityParam(*FindEntityClass("prop_model"), "up"),
              "entitylist.txt should have collision and up");
        World &w = e.world;
        ClearProps(w);
        int model = LoadPropModel(w, "models/props/foliage/fern_tall.obj");
        CHECK(model >= 0, "couldn't load the fern");
        float height = w.propModels[model].bounds.max.y;

        AddProp(w, model, { 0, 0, 0 }, { 0, 1, 0 }, 90.0f, 1.0f, false, false);
        CHECK(fabsf(w.props[0].bounds.max.y - height) < 0.01f, "upright: top at %f, not %f", w.props[0].bounds.max.y, height);
        CHECK(w.propColliders.empty(), "no collision should make no collider");

        AddProp(w, model, { 0, 0, 0 }, { 1, 0, 0 }, 0.0f, 1.0f, false, true);
        CHECK(fabsf(w.props[1].bounds.max.x - height) < 0.01f, "on its side: top at %f, not %f", w.props[1].bounds.max.x, height);
        CHECK(w.propColliders.size() == 1, "collision shouldn't need static");

        AddProp(w, model, { 0, 0, 0 }, { 0, -1, 0 }, 0.0f, 1.0f, true, false);
        CHECK(fabsf(w.props[2].bounds.min.y + height) < 0.01f, "hanging: top at %f, not %f", w.props[2].bounds.min.y, -height);
        CHECK(w.propColliders.size() == 1, "static shouldn't make a collider");
        ClearProps(w);
    }

    printf("22 the Sprinkler\n");
    {
        // A slab with a tall box standing on it, so a ring near the box reaches its wall and top
        GeoShape slab = Box(1);
        for (Vector3 &v : slab.vertices) { v.x *= 4.0f; v.z *= 8.0f; }
        Reset(e, slab);
        e.level.entities.clear();
        SetEditorMode(e, MODE_MODELLING);
        SetEditorMode(e, MODE_OBJECT);
        GeometryChanged(e);
        SyncWorldWithLevel(e);
        ResetUndoHistory(e);

        SprinklerState &s = e.sprinkler;
        s = SprinklerState();
        CHECK(SprinkleProps(e, { 0, 64, 0 }, { 0, 1, 0 }) == 0 && e.level.entities.empty(), "no props picked should place nothing");

        s.props[0] = "models/props/foliage/grass_short.obj";
        s.props[2] = "models/props/foliage/grass_bushy.obj";
        s.radius = 40.0f;
        s.count = 200;
        int placed = SprinkleProps(e, { 0, 64, 0 }, { 0, 1, 0 });
        CHECK(placed == 200, "the whole ring is on the slab, placed %d", placed);
        std::set<std::string> models;
        std::set<int> angles;
        float furthest = 0.0f;
        for (const LevelEntity &entity : e.level.entities)
        {
            CHECK(entity.classname == "prop_model", "made a %s", entity.classname.c_str());
            CHECK(fabsf(entity.origin.y - 64.0f) < 0.02f, "should rest on the surface, at %f", entity.origin.y);
            float distance = sqrtf(entity.origin.x*entity.origin.x + entity.origin.z*entity.origin.z);
            CHECK(distance <= 40.02f, "outside the ring by %f", distance - 40.0f);
            furthest = fmaxf(furthest, distance);
            CHECK(fabsf(EntityVectorOrDefault(entity, "up", { 0, 0, 0 }).y - 1.0f) < 0.001f, "should be upright on a flat top");
            CHECK(!EntityBoolOrDefault(entity, "collision", true), "collision should come from the tool");
            CHECK(!EntityBoolOrDefault(entity, "static", true), "static should come from the tool");
            float scale = EntityNumberOrDefault(entity, "scale", 0.0f);
            CHECK(scale >= 0.8f && scale <= 1.2f, "scale %f out of range", scale);
            models.insert(EntityTextOrDefault(entity, "model", ""));
            angles.insert((int)EntityNumberOrDefault(entity, "angle", 0.0f));
        }
        CHECK(models.size() == 2 && !models.count(""), "should use both props and skip the empty slot, used %d", (int)models.size());
        CHECK(angles.size() > 50, "angles should vary, got %d", (int)angles.size());
        CHECK(furthest > 30.0f, "should reach the ring's edge, furthest %f", furthest);

        CHECK(EntityBoolOrDefault(e.level.entities[0], "detailLighting", false), "detail lighting should be on by default");

        // Detail lighting: the spot in the lightmap is the slab's top right under the prop
        {
            World &w = e.world;
            ClearProps(w);
            int model = LoadPropModel(w, "models/props/foliage/grass_short.obj");
            Vector3 at = { 100, 64, -60 };
            AddProp(w, model, at, { 0, 1, 0 }, 0.0f, 1.0f, false, false, true);
            AddProp(w, model, { 100, 200, -60 }, { 0, 1, 0 }, 0.0f, 1.0f, false, false, true);  // Floating well over it
            AddProp(w, model, at, { 0, 1, 0 }, 0.0f, 1.0f, false, false, false);
            AddProp(w, model, at, { 0, 1, 0 }, 0.0f, 1.0f, true, false, true);
            FindPropBounceSpots(w);
            CHECK(w.props[0].hasBounceSpot, "should find the ground under it");
            CHECK(!w.props[1].hasBounceSpot, "floating far over the ground should find none");
            CHECK(!w.props[2].hasBounceSpot, "detail lighting off should find none");
            CHECK(!w.props[3].hasBounceSpot, "static ones have their own lightmap");

            // The lightmap texel nearest the spot should be on the top, near the prop
            Vector2 spot = { w.props[0].bounceSpot.x*w.lightmapWidth, w.props[0].bounceSpot.y*w.lightmapHeight };
            const LightmapTexel *nearest = nullptr;
            float best = 1e9f;
            for (const LightmapTexel &texel : w.lightmapTexels)
            {
                float d = Vector2Distance(spot, { texel.x + 0.5f, texel.y + 0.5f });
                if (d < best) { best = d; nearest = &texel; }
            }
            CHECK(nearest && best < 1.5f && nearest->normal.y > 0.99f && Vector3Distance(nearest->position, at) < 24.0f,
                  "the spot should be the top under the prop, texel %f away, at (%f %f %f)", best,
                  nearest ? nearest->position.x : 0, nearest ? nearest->position.y : 0, nearest ? nearest->position.z : 0);
            ClearProps(w);
        }

        Undo(e);
        CHECK(e.level.entities.empty(), "one undo should take back the whole click, left %d", (int)e.level.entities.size());

        // Off the edge of the slab: x ends at 256, and spots past it find nothing near
        s.radius = 64.0f;
        s.count = 100;
        placed = SprinkleProps(e, { 256, 64, 0 }, { 0, 1, 0 });
        CHECK(placed == 100, "attempts should make up for the half that misses, placed %d", placed);
        for (const LevelEntity &entity : e.level.entities) CHECK(entity.origin.x <= 256.01f && fabsf(entity.origin.y - 64.0f) < 0.02f, "fell off the edge to (%f %f)", entity.origin.x, entity.origin.y);
        e.level.entities.clear();

        // Clicked on the side: props stand out from it, and nothing lands on the top next to it
        s.radius = 20.0f;
        s.count = 50;
        placed = SprinkleProps(e, { 256, 40, 0 }, { 1, 0, 0 });
        CHECK(placed > 0, "should place props on a wall");
        for (const LevelEntity &entity : e.level.entities)
        {
            CHECK(fabsf(entity.origin.x - 256.0f) < 0.02f, "should rest on the wall, at x %f", entity.origin.x);
            CHECK(fabsf(EntityVectorOrDefault(entity, "up", { 0, 0, 0 }).x - 1.0f) < 0.001f, "should stand out from the wall");
        }
        e.level.entities.clear();

        // Through the real input: P turns it on, a click on the level places them, a click on the sky doesn't
        ResetUndoHistory(e);
        s.radius = 16.0f;
        s.count = 5;
        Rectangle view = GetViewportRect();
        AimCamera(e, { 0, 256, 128 }, { 0, 64, 0 });
        auto frame = [&](Vector2 mouse, bool pressed, std::set<int> keys = {})
        {
            editInputScript.mouse = mouse;
            editInputScript.leftDown = editInputScript.leftPressed = pressed;
            editInputScript.keysPressed = keys;
            e.keysFree = true;
            UpdateViewport(e);
        };
        Vector2 screen = GetWorldToScreenEx({ 0, 64, 0 }, e.camera, (int)view.width, (int)view.height);
        Vector2 onLevel = { view.x + screen.x, view.y + screen.y };
        frame(onLevel, true);
        CHECK(e.level.entities.empty() && !s.active, "clicks shouldn't sprinkle until it's on");
        frame(onLevel, false, { KEY_P });
        CHECK(s.active, "P should turn it on");
        frame(onLevel, false);
        CHECK(s.hovering && fabsf(s.hoverPoint.y - 64.0f) < 0.01f, "should find the surface under the mouse");
        frame(onLevel, true);
        CHECK((int)e.level.entities.size() == 5, "a click should place 5, placed %d", (int)e.level.entities.size());
        AimCamera(e, { 0, 256, 128 }, { 0, 1024, 128 });
        frame(onLevel, true);
        CHECK((int)e.level.entities.size() == 5 && !s.hovering, "clicking the sky shouldn't place anything");
        frame(onLevel, false, { KEY_P });
        CHECK(!s.active, "P should turn it off");

        s.props[0] = s.props[2] = "";
        ToggleSprinkler(e);
        SetEditorMode(e, MODE_MODELLING);
        CHECK(!s.active, "leaving Object mode should turn it off");
        editInputScript.keysPressed.clear();
        SetEditorMode(e, MODE_OBJECT);
    }

    printf("23 transparency: cutout textures, their edges, and drawing them\n");
    {
        // A red texel in a see-through 3x3: the rest take its color, and stay see-through
        Image image = GenImageColor(3, 3, BLANK);
        ImageDrawPixel(&image, 1, 1, RED);
        CHECK(FixAlphaEdges(image), "should be a cutout");
        Color *pixels = (Color *)image.data;
        CHECK(pixels[0].r == 230 && pixels[0].a == 0 && pixels[8].r == 230 && pixels[8].a == 0, "corners should take the red, got %d %d (alpha %d)", pixels[0].r, pixels[8].r, pixels[0].a);
        CHECK(pixels[4].r == 230 && pixels[4].a == 255, "the visible texel should stay as it was");
        UnloadImage(image);

        // Half see-through counts too, and only fully see-through texels get filled
        image = GenImageColor(4, 1, { 0, 0, 255, 255 });
        ImageDrawPixel(&image, 0, 0, { 0, 255, 0, 100 });
        ImageDrawPixel(&image, 2, 0, { 0, 0, 0, 0 });
        CHECK(FixAlphaEdges(image), "partly see-through should be a cutout");
        pixels = (Color *)image.data;
        CHECK(pixels[0].g == 255 && pixels[0].a == 100, "a partly see-through texel keeps its color");
        CHECK(pixels[2].b > 0 && pixels[2].a == 0, "a see-through texel between visible ones takes their colors");
        UnloadImage(image);

        image = GenImageColor(2, 2, WHITE);
        CHECK(!FixAlphaEdges(image), "fully opaque isn't a cutout");
        UnloadImage(image);
        image = GenImageColor(2, 2, WHITE);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        CHECK(!FixAlphaEdges(image) && image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8, "no alpha channel isn't a cutout, and stays as it was");
        UnloadImage(image);

        Texture grass = { 0 }, wall = { 0 };
        CHECK(LoadGameTexture("models/props/foliage/TUFTA01.png", grass) && IsTextureCutout(grass.id), "the grass texture should be a cutout");
        CHECK(LoadGameTexture(MAT, wall) && !IsTextureCutout(wall.id), "the wall texture shouldn't be");
        UnloadTexture(grass);
        UnloadTexture(wall);

        // Drawn: a grass card seen edge on to its plane from behind still shows, and its holes show what's behind
        World &w = e.world;
        ClearProps(w);
        int model = LoadPropModel(w, "models/props/foliage/grass_short.obj");
        CHECK(model >= 0 && IsTextureCutout(w.propModels[model].model.materials[w.propModels[model].model.meshMaterial[0]].maps[MATERIAL_MAP_DIFFUSE].texture.id),
              "the grass prop's texture should be a cutout");
        w.propsHidden = false;
        AddProp(w, model, { 0, 64, 0 }, { 0, 1, 0 }, 0.0f, 4.0f, false, false, false);
        RenderTexture2D target = LoadRenderTexture(256, 256);
        auto render = [&](Vector3 from, Vector3 to)
        {
            // The game's own drawing, with the slab's meshes hidden so only the grass is in front of the background
            Camera camera = { from, to, { 0, 1, 0 }, 60.0f, CAMERA_PERSPECTIVE };
            int meshCount = w.model.meshCount;
            w.model.meshCount = 0;
            RenderWorldShadows(w);
            BeginTextureMode(target);
                ClearBackground(MAGENTA);
                BeginMode3D(camera);
                    DrawWorld(w, false);
                EndMode3D();
            EndTextureMode();
            w.model.meshCount = meshCount;
            Image shot = LoadImageFromTexture(target.texture);
            int covered = 0;
            for (int i = 0; i < shot.width*shot.height; i++)
            {
                Color c = ((Color *)shot.data)[i];
                if (!(c.r == 255 && c.g == 0 && c.b == 255)) covered++;
            }
            UnloadImage(shot);
            return covered;
        };
        auto above = render({ 0, 200, 1 }, { 0, 64, 0 });
        auto below = render({ 0, 20, 1 }, { 0, 64, 0 });  // From under the cards, at their backs
        printf("    grass covers %d pixels from above, %d from below\n", above, below);
        // Without cutouts it covered about 22200 from above, since the see-through parts of near cards still hid the
        // cards behind them, and about 1100 from below, with the backs culled. With them, about 25400 and 54000
        CHECK(above > 23800, "see-through texels shouldn't hide the cards behind them, covered %d", above);
        CHECK(above < 256*256*0.9, "its holes should show the background, covered %d", above);
        CHECK(below > 10000, "should see the grass's back faces too, %d pixels", below);
        UnloadRenderTexture(target);
        ClearProps(w);
    }

    printf("\n%d checks, %d failed\n", checks, failures);
    fflush(stdout);
    _exit(failures ? 1 : 0);
}
