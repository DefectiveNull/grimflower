#include "geometry.h"
#include "gamedata.h"
#include "raymath.h"
#include "world.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>

static const float ON_PLANE_EPSILON = 0.01f;   // World units: points this close to a plane are on it

Matrix GeoMeshTransform(const GeoMesh &mesh)
{
    return MatrixMultiply(QuaternionToMatrix(mesh.rotation), MatrixTranslate(mesh.position.x, mesh.position.y, mesh.position.z));
}

std::vector<Vector3> GeoFacePoints(const GeoShape &shape, const GeoFace &face, const Matrix &transform)
{
    std::vector<Vector3> points;
    for (int corner : face.corners) points.push_back(Vector3Transform(shape.vertices[corner], transform));
    return points;
}

Vector3 GeoPolygonNormal(const std::vector<Vector3> &points)
{
    Vector3 normal = { 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < points.size(); i++)
    {
        Vector3 a = points[i], b = points[(i + 1)%points.size()];
        normal.x += (a.y - b.y)*(a.z + b.z);
        normal.y += (a.z - b.z)*(a.x + b.x);
        normal.z += (a.x - b.x)*(a.y + b.y);
    }
    return (Vector3LengthSqr(normal) > 0.0f) ? Vector3Normalize(normal) : Vector3{ 0.0f, 1.0f, 0.0f };
}

void GeoParaxialAxes(Vector3 normal, Vector3 &uAxis, Vector3 &vAxis)
{
    // Unlike Quake's, the horizontal direction flips with the side a wall faces, so textures read the right
    // way round from in front of every wall instead of mirrored on half of them
    Vector3 a = { fabsf(normal.x), fabsf(normal.y), fabsf(normal.z) };
    if (a.y >= a.x && a.y >= a.z)
    {
        uAxis = { 1.0f, 0.0f, 0.0f };   // Floors and ceilings
        vAxis = { 0.0f, 0.0f, (normal.y >= 0.0f) ? 1.0f : -1.0f };
    }
    else if (a.x >= a.z)
    {
        uAxis = { 0.0f, 0.0f, (normal.x >= 0.0f) ? -1.0f : 1.0f };   // Walls facing along x
        vAxis = { 0.0f, -1.0f, 0.0f };
    }
    else
    {
        uAxis = { (normal.z >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f };   // Walls facing along z
        vAxis = { 0.0f, -1.0f, 0.0f };
    }
}

void GeoResetTexture(GeoFace &face, Vector3 worldNormal)
{
    GeoParaxialAxes(worldNormal, face.uAxis, face.vAxis);
    face.offset = { 0.0f, 0.0f };
    face.scale = { 1.0f, 1.0f };
}

Vector2 GeoTextureCoord(const GeoFace &face, Vector3 point, int textureWidth, int textureHeight)
{
    float s = Vector3DotProduct(point, face.uAxis)/face.scale.x + face.offset.x;
    float t = Vector3DotProduct(point, face.vAxis)/face.scale.y + face.offset.y;
    return { s/(float)std::max(textureWidth, 1), t/(float)std::max(textureHeight, 1) };
}

// ---------------------------------------------------------------------------------------------
// Triangulating

std::vector<int> GeoTriangulate(const std::vector<Vector3> &points, Vector3 normal)
{
    std::vector<int> triangles;
    int count = (int)points.size();
    if (count < 3) return triangles;
    if (count == 3) return { 0, 1, 2 };

    // Flattened onto the polygon's plane, seen from the side the normal points to
    Vector3 reference = (fabsf(normal.y) < 0.9f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    Vector3 u = Vector3Normalize(Vector3CrossProduct(reference, normal));
    Vector3 v = Vector3CrossProduct(normal, u);
    std::vector<Vector2> flat(count);
    for (int i = 0; i < count; i++) flat[i] = { Vector3DotProduct(points[i], u), Vector3DotProduct(points[i], v) };

    auto cross = [](Vector2 a, Vector2 b, Vector2 c) { return (b.x - a.x)*(c.y - a.y) - (b.y - a.y)*(c.x - a.x); };

    // Ear clipping: cut off a corner whose triangle turns the polygon's way and has no other corner in it
    std::vector<int> remaining(count);
    for (int i = 0; i < count; i++) remaining[i] = i;
    int guard = count*count;
    while (remaining.size() > 3 && guard-- > 0)
    {
        bool clipped = false;
        int n = (int)remaining.size();
        for (int i = 0; i < n; i++)
        {
            int ia = remaining[(i + n - 1)%n], ib = remaining[i], ic = remaining[(i + 1)%n];
            Vector2 a = flat[ia], b = flat[ib], c = flat[ic];
            if (cross(a, b, c) <= 1e-6f) continue;  // Reflex or flat: not an ear

            bool empty = true;
            for (int j : remaining)
            {
                if (j == ia || j == ib || j == ic) continue;
                Vector2 p = flat[j];
                if (cross(a, b, p) >= 0.0f && cross(b, c, p) >= 0.0f && cross(c, a, p) >= 0.0f) { empty = false; break; }
            }
            if (!empty) continue;

            triangles.insert(triangles.end(), { ia, ib, ic });
            remaining.erase(remaining.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped)
        {
            // Only flat corners left (points in a straight line), or a polygon that crosses itself: a fan
            // covers what's left
            for (size_t i = 1; i + 1 < remaining.size(); i++) triangles.insert(triangles.end(), { remaining[0], remaining[i], remaining[i + 1] });
            return triangles;
        }
    }
    if (remaining.size() == 3) triangles.insert(triangles.end(), { remaining[0], remaining[1], remaining[2] });
    return triangles;
}

// ---------------------------------------------------------------------------------------------
// Convex pieces

struct BspPolygon
{
    std::vector<Vector3> points;
    Vector3 normal;
    float dist;
};

// Cuts a polygon by a plane into the parts in front of it and behind it. Points within ON_PLANE_EPSILON of
// the plane count as on it, and go to both
static void SplitPolygon(const std::vector<Vector3> &points, Vector3 normal, float dist, std::vector<Vector3> &front, std::vector<Vector3> &back)
{
    front.clear();
    back.clear();
    size_t count = points.size();
    for (size_t i = 0; i < count; i++)
    {
        Vector3 a = points[i], b = points[(i + 1)%count];
        float da = Vector3DotProduct(normal, a) - dist, db = Vector3DotProduct(normal, b) - dist;
        if (da >= -ON_PLANE_EPSILON) front.push_back(a);
        if (da <= ON_PLANE_EPSILON) back.push_back(a);
        if ((da > ON_PLANE_EPSILON && db < -ON_PLANE_EPSILON) || (da < -ON_PLANE_EPSILON && db > ON_PLANE_EPSILON))
        {
            Vector3 cut = Vector3Lerp(a, b, da/(da - db));
            front.push_back(cut);
            back.push_back(cut);
        }
    }
}

// Which side of the plane a polygon is on: 1 in front, -1 behind, 0 on it, 2 across it
static int PolygonSide(const BspPolygon &polygon, Vector3 normal, float dist)
{
    bool anyFront = false, anyBack = false;
    for (Vector3 p : polygon.points)
    {
        float d = Vector3DotProduct(normal, p) - dist;
        if (d > ON_PLANE_EPSILON) anyFront = true;
        if (d < -ON_PLANE_EPSILON) anyBack = true;
    }
    if (anyFront && anyBack) return 2;
    if (anyFront) return 1;
    if (anyBack) return -1;
    return 0;
}

// The faces of the convex region inside every plane (normals pointing out), within the box
static std::vector<std::vector<Vector3>> RegionFaces(const std::vector<Plane> &planes)
{
    std::vector<std::vector<Vector3>> faces;
    for (size_t i = 0; i < planes.size(); i++)
    {
        // A huge square on this plane, cut down by all the others
        Vector3 n = planes[i].normal;
        Vector3 reference = (fabsf(n.y) < 0.9f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
        Vector3 u = Vector3Scale(Vector3Normalize(Vector3CrossProduct(reference, n)), 65536.0f);
        Vector3 v = Vector3Scale(Vector3Normalize(Vector3CrossProduct(n, Vector3Normalize(u))), 65536.0f);
        Vector3 center = Vector3Scale(n, planes[i].dist);
        // Wound counter-clockwise seen from outside (from the side the normal points to)
        std::vector<Vector3> polygon = { Vector3Subtract(Vector3Subtract(center, u), v), Vector3Subtract(Vector3Add(center, u), v),
                                         Vector3Add(Vector3Add(center, u), v), Vector3Add(Vector3Subtract(center, u), v) };
        if (Vector3DotProduct(GeoPolygonNormal(polygon), n) < 0.0f) std::reverse(polygon.begin(), polygon.end());

        for (size_t j = 0; j < planes.size() && polygon.size() >= 3; j++)
        {
            if (j == i) continue;
            std::vector<Vector3> front, back;
            SplitPolygon(polygon, planes[j].normal, planes[j].dist, front, back);
            polygon = back;
        }
        if (polygon.size() < 3) continue;

        // Drop points that ended up on top of each other
        std::vector<Vector3> cleaned;
        for (Vector3 p : polygon)
        {
            if (cleaned.empty() || Vector3Distance(cleaned.back(), p) > ON_PLANE_EPSILON) cleaned.push_back(p);
        }
        while (cleaned.size() > 1 && Vector3Distance(cleaned.front(), cleaned.back()) <= ON_PLANE_EPSILON) cleaned.pop_back();
        if (cleaned.size() >= 3) faces.push_back(cleaned);
    }
    return faces;
}

std::vector<std::vector<std::vector<Vector3>>> GeoConvexPieces(const GeoShape &shape)
{
    std::vector<std::vector<std::vector<Vector3>>> pieces;

    std::vector<BspPolygon> polygons;
    BoundingBox bounds = { { FLT_MAX, FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX, -FLT_MAX } };
    for (const GeoFace &face : shape.faces)
    {
        BspPolygon polygon;
        for (int corner : face.corners) polygon.points.push_back(shape.vertices[corner]);
        if (polygon.points.size() < 3) continue;
        polygon.normal = GeoPolygonNormal(polygon.points);
        polygon.dist = Vector3DotProduct(polygon.normal, polygon.points[0]);
        for (Vector3 p : polygon.points)
        {
            bounds.min = Vector3Min(bounds.min, p);
            bounds.max = Vector3Max(bounds.max, p);
        }
        polygons.push_back(polygon);
    }
    if (polygons.empty()) return pieces;

    // The box around the shape, a little bigger, keeps every region bounded even if the shape has a gap
    bounds.min = Vector3SubtractValue(bounds.min, 1.0f);
    bounds.max = Vector3AddValue(bounds.max, 1.0f);
    std::vector<Plane> constraints = {
        { { 1, 0, 0 }, bounds.max.x }, { { -1, 0, 0 }, -bounds.min.x },
        { { 0, 1, 0 }, bounds.max.y }, { { 0, -1, 0 }, -bounds.min.y },
        { { 0, 0, 1 }, bounds.max.z }, { { 0, 0, -1 }, -bounds.min.z },
    };

    // Each split picks the face plane that cuts the fewest other faces, so convex shapes come out whole.
    // Whatever is behind a plane with nothing left behind it is solid: that's a convex piece, inside every
    // plane on the way down
    std::function<void(const std::vector<BspPolygon> &, int)> build = [&](const std::vector<BspPolygon> &list, int depth)
    {
        int best = 0, bestScore = INT_MAX;
        for (size_t i = 0; i < list.size(); i++)
        {
            int splits = 0, front = 0, back = 0;
            for (size_t j = 0; j < list.size(); j++)
            {
                if (j == i) continue;
                int side = PolygonSide(list[j], list[i].normal, list[i].dist);
                if (side == 2) splits++;
                else if (side == 1) front++;
                else if (side == -1) back++;
            }
            int score = splits*8 + abs(front - back);
            if (score < bestScore) { bestScore = score; best = (int)i; }
        }

        Vector3 normal = list[best].normal;
        float dist = list[best].dist;
        std::vector<BspPolygon> frontList, backList;
        for (const BspPolygon &polygon : list)
        {
            int side = PolygonSide(polygon, normal, dist);
            if (side == 0)
            {
                // On the splitting plane. Facing the same way, it's done with. Facing the other way, it's the
                // side of solid in front of the plane (two parts of the shape back to back), so it stays for that side
                if (Vector3DotProduct(polygon.normal, normal) < 0.0f) frontList.push_back(polygon);
                continue;
            }
            if (side == 1) frontList.push_back(polygon);
            else if (side == -1) backList.push_back(polygon);
            else
            {
                BspPolygon front = polygon, back = polygon;
                SplitPolygon(polygon.points, normal, dist, front.points, back.points);
                if (front.points.size() >= 3) frontList.push_back(front);
                if (back.points.size() >= 3) backList.push_back(back);
            }
        }

        if (!frontList.empty() && depth < 64)
        {
            constraints.push_back({ Vector3Negate(normal), -dist });  // In front: inside the flipped plane
            build(frontList, depth + 1);
            constraints.pop_back();
        }

        constraints.push_back({ normal, dist });
        if (backList.empty() || depth >= 64)
        {
            std::vector<std::vector<Vector3>> faces = RegionFaces(constraints);
            if (faces.size() >= 4) pieces.push_back(faces);
        }
        else
        {
            build(backList, depth + 1);
        }
        constraints.pop_back();
    };
    build(polygons, 0);
    return pieces;
}

// ---------------------------------------------------------------------------------------------
// Merging faces back into polygroups

static bool SameTexture(const GeoFace &a, const GeoFace &b)
{
    return a.material == b.material && Vector3Equals(a.uAxis, b.uAxis) && Vector3Equals(a.vAxis, b.vAxis) &&
           Vector2Equals(a.offset, b.offset) && Vector2Equals(a.scale, b.scale);
}

// Joins vertices in the same place (tools like extrude can leave two on one spot), drops faces that come out
// with fewer than three corners, and removes pairs of faces that are the same polygon facing opposite ways:
// two parts of the shape back to back, whose touching sides are inside it
static void CleanShape(GeoShape &shape)
{
    std::vector<int> same(shape.vertices.size());
    for (size_t i = 0; i < shape.vertices.size(); i++)
    {
        same[i] = (int)i;
        for (size_t j = 0; j < i; j++)
        {
            if (same[j] == (int)j && Vector3Distance(shape.vertices[i], shape.vertices[j]) < ON_PLANE_EPSILON) { same[i] = (int)j; break; }
        }
    }

    std::vector<GeoFace> faces;
    for (GeoFace face : shape.faces)
    {
        std::vector<int> corners;
        for (int corner : face.corners)
        {
            int c = same[corner];
            if (corners.empty() || corners.back() != c) corners.push_back(c);
        }
        while (corners.size() > 1 && corners.front() == corners.back()) corners.pop_back();
        if (corners.size() < 3) continue;
        face.corners = corners;
        faces.push_back(face);
    }

    // A face's corners, starting from its lowest, to compare faces whatever corner they start at
    auto canonical = [](std::vector<int> corners)
    {
        std::rotate(corners.begin(), std::min_element(corners.begin(), corners.end()), corners.end());
        return corners;
    };
    std::vector<bool> removed(faces.size(), false);
    for (size_t i = 0; i < faces.size(); i++)
    {
        if (removed[i]) continue;
        std::vector<int> reversed(faces[i].corners.rbegin(), faces[i].corners.rend());
        std::vector<int> key = canonical(reversed);
        for (size_t j = i + 1; j < faces.size(); j++)
        {
            if (removed[j] || faces[j].corners.size() != reversed.size() || canonical(faces[j].corners) != key) continue;
            removed[i] = removed[j] = true;
            break;
        }
    }
    shape.faces.clear();
    for (size_t i = 0; i < faces.size(); i++) if (!removed[i]) shape.faces.push_back(faces[i]);
}

void GeoMergeFaces(GeoShape &shape)
{
    CleanShape(shape);

    // Faces grouped by plane and texture, then joined where they share an edge
    std::vector<Vector3> normals(shape.faces.size());
    std::vector<float> dists(shape.faces.size());
    for (size_t i = 0; i < shape.faces.size(); i++)
    {
        std::vector<Vector3> points;
        for (int corner : shape.faces[i].corners) points.push_back(shape.vertices[corner]);
        normals[i] = GeoPolygonNormal(points);
        dists[i] = points.empty() ? 0.0f : Vector3DotProduct(normals[i], points[0]);
    }

    std::vector<int> parent(shape.faces.size());
    for (size_t i = 0; i < parent.size(); i++) parent[i] = (int)i;
    std::function<int(int)> root = [&](int i) { while (parent[i] != i) i = parent[i] = parent[parent[i]]; return i; };

    std::map<std::pair<int, int>, int> edgeFace;  // Directed edge -> the face it belongs to
    for (size_t i = 0; i < shape.faces.size(); i++)
    {
        const std::vector<int> &c = shape.faces[i].corners;
        for (size_t k = 0; k < c.size(); k++) edgeFace[{ c[k], c[(k + 1)%c.size()] }] = (int)i;
    }
    for (size_t i = 0; i < shape.faces.size(); i++)
    {
        const std::vector<int> &c = shape.faces[i].corners;
        for (size_t k = 0; k < c.size(); k++)
        {
            auto other = edgeFace.find({ c[(k + 1)%c.size()], c[k] });  // The same edge, the other way round
            if (other == edgeFace.end() || other->second == (int)i) continue;
            int j = other->second;
            if (Vector3DotProduct(normals[i], normals[j]) < 0.9999f || fabsf(dists[i] - dists[j]) > ON_PLANE_EPSILON) continue;
            if (!SameTexture(shape.faces[i], shape.faces[j])) continue;
            parent[root((int)i)] = root(j);
        }
    }

    std::map<int, std::vector<int>> groups;
    for (size_t i = 0; i < shape.faces.size(); i++) groups[root((int)i)].push_back((int)i);

    std::vector<GeoFace> merged;
    for (auto &[groupRoot, members] : groups)
    {
        if (members.size() == 1)
        {
            merged.push_back(shape.faces[members[0]]);
            continue;
        }

        // The group's outline: its edges that aren't shared by two of its faces, chained into loops
        std::set<std::pair<int, int>> edges;
        for (int f : members)
        {
            const std::vector<int> &c = shape.faces[f].corners;
            for (size_t k = 0; k < c.size(); k++) edges.insert({ c[k], c[(k + 1)%c.size()] });
        }
        std::map<int, int> next;
        bool branching = false;
        for (const auto &edge : edges)
        {
            if (edges.count({ edge.second, edge.first })) continue;
            if (next.count(edge.first)) branching = true;
            next[edge.first] = edge.second;
        }

        std::vector<std::vector<int>> loops;
        std::set<int> used;
        for (const auto &[start, unused] : next)
        {
            if (used.count(start) || branching) continue;
            std::vector<int> loop;
            int at = start;
            while (!used.count(at) && next.count(at))
            {
                used.insert(at);
                loop.push_back(at);
                at = next[at];
            }
            if (at != start) branching = true;  // Didn't come back round: not a clean outline
            loops.push_back(loop);
        }

        // One loop is one polygon. More than one would be a polygon with a hole: those stay as they are
        if (branching || loops.size() != 1 || loops[0].size() < 3)
        {
            for (int f : members) merged.push_back(shape.faces[f]);
            continue;
        }
        GeoFace face = shape.faces[members[0]];
        face.corners = loops[0];
        merged.push_back(face);
    }
    shape.faces = merged;

    // Corners left in the middle of a straight edge, by a cut that's gone now, are dropped: ones only two
    // faces share, with the same two corners either side in both, in a line
    std::vector<int> uses(shape.vertices.size(), 0);
    for (const GeoFace &face : shape.faces) for (int corner : face.corners) uses[corner]++;
    for (size_t v = 0; v < shape.vertices.size(); v++)
    {
        if (uses[v] != 2) continue;
        std::vector<std::pair<GeoFace *, size_t>> at;
        for (GeoFace &face : shape.faces)
        {
            for (size_t k = 0; k < face.corners.size(); k++) if (face.corners[k] == (int)v) at.push_back({ &face, k });
        }
        if (at.size() != 2 || at[0].first == at[1].first || at[0].first->corners.size() <= 3 || at[1].first->corners.size() <= 3) continue;
        auto neighbours = [](const GeoFace &face, size_t k)
        {
            size_t n = face.corners.size();
            return std::pair<int, int>{ face.corners[(k + n - 1)%n], face.corners[(k + 1)%n] };
        };
        std::pair<int, int> a = neighbours(*at[0].first, at[0].second), b = neighbours(*at[1].first, at[1].second);
        if (!(a.first == b.second && a.second == b.first)) continue;  // The other face runs the other way round
        Vector3 p = shape.vertices[a.first], q = shape.vertices[a.second], m = shape.vertices[v];
        Vector3 along = Vector3Subtract(q, p);
        float length = Vector3Length(along);
        if (length < ON_PLANE_EPSILON) continue;
        Vector3 offset = Vector3Subtract(m, p);
        float t = Vector3DotProduct(offset, along)/(length*length);
        if (t <= 0.0f || t >= 1.0f || Vector3Length(Vector3Subtract(offset, Vector3Scale(along, t))) > ON_PLANE_EPSILON) continue;
        for (auto &[face, k] : at) face->corners.erase(face->corners.begin() + k);
        uses[v] = 0;
    }

    // Vertices no face uses any more
    std::vector<int> remap(shape.vertices.size(), -1);
    std::vector<Vector3> vertices;
    for (GeoFace &face : shape.faces)
    {
        for (int &corner : face.corners)
        {
            if (remap[corner] < 0)
            {
                remap[corner] = (int)vertices.size();
                vertices.push_back(shape.vertices[corner]);
            }
            corner = remap[corner];
        }
    }
    shape.vertices = vertices;
}

// ---------------------------------------------------------------------------------------------
// Importing OBJs

// Texture axes, scale and offset that put the face's texture where the OBJ's texture coordinates have it.
// Texture coordinates are a flat (affine) mapping on each face, which this finds exactly from three corners
static void FitTexture(GeoFace &face, const std::vector<Vector3> &points, const std::vector<Vector2> &uvs, Vector3 normal)
{
    GeoResetTexture(face, normal);
    int width = 64, height = 64;
    if (face.material.empty() || !GetGameTextureSize(face.material.c_str(), width, height)) return;
    if (points.size() < 3) return;

    // The first three corners that aren't in a line
    size_t i1 = 1, i2 = 2;
    bool found = false;
    for (i1 = 1; i1 < points.size() && !found; i1++)
    {
        for (i2 = i1 + 1; i2 < points.size(); i2++)
        {
            Vector3 c = Vector3CrossProduct(Vector3Subtract(points[i1], points[0]), Vector3Subtract(points[i2], points[0]));
            if (Vector3LengthSqr(c) > 1e-6f) { found = true; break; }
        }
        if (found) break;
    }
    if (!found) return;

    Vector3 e1 = Vector3Subtract(points[i1], points[0]), e2 = Vector3Subtract(points[i2], points[0]);
    float a11 = Vector3DotProduct(e1, e1), a12 = Vector3DotProduct(e1, e2), a22 = Vector3DotProduct(e2, e2);
    float det = a11*a22 - a12*a12;
    if (fabsf(det) < 1e-9f) return;

    // A direction g with dot(g, e1) = d1 and dot(g, e2) = d2, lying in the face's plane
    auto solve = [&](float d1, float d2)
    {
        float a = (d1*a22 - d2*a12)/det, b = (d2*a11 - d1*a12)/det;
        return Vector3Add(Vector3Scale(e1, a), Vector3Scale(e2, b));
    };
    float s0 = uvs[0].x*width, s1 = uvs[i1].x*width, s2 = uvs[i2].x*width;
    float t0 = uvs[0].y*height, t1 = uvs[i1].y*height, t2 = uvs[i2].y*height;
    Vector3 gu = solve(s1 - s0, s2 - s0), gv = solve(t1 - t0, t2 - t0);
    float lu = Vector3Length(gu), lv = Vector3Length(gv);
    if (lu < 1e-6f || lv < 1e-6f) return;  // No texture coordinates: the paraxial default stays

    face.uAxis = Vector3Scale(gu, 1.0f/lu);
    face.vAxis = Vector3Scale(gv, 1.0f/lv);
    face.scale = { 1.0f/lu, 1.0f/lv };
    face.offset = { s0 - Vector3DotProduct(points[0], gu), t0 - Vector3DotProduct(points[0], gv) };
}

static std::string TrimLine(std::istringstream &tokens)
{
    std::string rest;
    std::getline(tokens, rest);
    size_t start = rest.find_first_not_of(" \t\r");
    size_t end = rest.find_last_not_of(" \t\r");
    return (start == std::string::npos) ? "" : rest.substr(start, end - start + 1);
}

bool ImportObjGeometry(const char *objPath, float scale, Geometry &geometry)
{
    char *text = LoadFileText(objPath);
    if (text == nullptr) return false;
    std::istringstream file(text);
    UnloadFileText(text);
    std::string objDir = GetDirectoryPath(objPath);

    struct ObjFace { std::vector<int> positions; std::vector<int> texcoords; std::string material; };
    std::vector<Vector3> positions;
    std::vector<Vector2> texcoords;
    std::map<std::string, std::string> mtlTextures;  // Material name -> its texture's game data path
    std::vector<std::vector<ObjFace>> objects;
    std::string material;

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream tokens(line);
        std::string tag;
        tokens >> tag;
        if (tag == "v")
        {
            Vector3 v;
            tokens >> v.x >> v.y >> v.z;
            positions.push_back(Vector3Scale(v, scale));
        }
        else if (tag == "vt")
        {
            Vector2 vt;
            tokens >> vt.x >> vt.y;
            texcoords.push_back({ vt.x, 1.0f - vt.y });  // OBJ's v goes up, textures go down
        }
        else if (tag == "o" || tag == "g") objects.emplace_back();
        else if (tag == "mtllib")
        {
            char *mtl = LoadFileText((objDir + "/" + TrimLine(tokens)).c_str());
            if (mtl == nullptr) continue;
            std::istringstream mtlFile(mtl);
            UnloadFileText(mtl);
            std::string mtlLine, name;
            while (std::getline(mtlFile, mtlLine))
            {
                std::istringstream mtlTokens(mtlLine);
                std::string mtlTag;
                mtlTokens >> mtlTag;
                if (mtlTag == "newmtl") name = TrimLine(mtlTokens);
                else if (mtlTag == "map_Kd" && !name.empty())
                {
                    // Game data paths, like Quake's; or relative to the OBJ, which is how most exporters write them
                    std::string path = TrimLine(mtlTokens);
                    if (!GameDataFileExists(path.c_str())) path = objDir + "/" + path;
                    mtlTextures[name] = path;
                }
            }
        }
        else if (tag == "usemtl")
        {
            auto found = mtlTextures.find(TrimLine(tokens));
            material = (found != mtlTextures.end()) ? found->second : "";
        }
        else if (tag == "f")
        {
            if (objects.empty()) objects.emplace_back();
            ObjFace face;
            face.material = material;
            std::string entry;
            while (tokens >> entry)
            {
                size_t slash = entry.find('/');
                int p = atoi(entry.substr(0, slash).c_str());
                face.positions.push_back((p < 0) ? (int)positions.size() + p : p - 1);
                int t = 0;
                if (slash != std::string::npos) t = atoi(entry.substr(slash + 1, entry.find('/', slash + 1) - slash - 1).c_str());
                face.texcoords.push_back((t == 0) ? -1 : (t < 0) ? (int)texcoords.size() + t : t - 1);
            }
            if (face.positions.size() >= 3) objects.back().push_back(face);
        }
    }

    for (const std::vector<ObjFace> &object : objects)
    {
        if (object.empty()) continue;

        // The object's own vertices, around the middle of its bounds
        std::map<int, int> local;
        BoundingBox box = { { FLT_MAX, FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX, -FLT_MAX } };
        for (const ObjFace &face : object)
        {
            for (int p : face.positions)
            {
                if (p < 0 || p >= (int)positions.size()) continue;
                box.min = Vector3Min(box.min, positions[p]);
                box.max = Vector3Max(box.max, positions[p]);
            }
        }
        Vector3 center = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);

        GeoShape shape;
        for (const ObjFace &objFace : object)
        {
            GeoFace face;
            face.material = objFace.material;
            std::vector<Vector3> points;
            std::vector<Vector2> uvs;
            bool valid = true;
            for (size_t k = 0; k < objFace.positions.size(); k++)
            {
                int p = objFace.positions[k];
                if (p < 0 || p >= (int)positions.size()) { valid = false; break; }
                if (!local.count(p))
                {
                    local[p] = (int)shape.vertices.size();
                    shape.vertices.push_back(Vector3Subtract(positions[p], center));
                }
                face.corners.push_back(local[p]);
                points.push_back(positions[p]);
                int t = objFace.texcoords[k];
                uvs.push_back((t >= 0 && t < (int)texcoords.size()) ? texcoords[t] : Vector2{ 0.0f, 0.0f });
            }
            if (!valid) continue;
            FitTexture(face, points, uvs, GeoPolygonNormal(points));
            shape.faces.push_back(face);
        }
        if (shape.faces.empty()) continue;

        geometry.meshes.push_back({ (int)geometry.shapes.size(), center, { 0.0f, 0.0f, 0.0f, 1.0f } });
        geometry.shapes.push_back(shape);
    }

    TraceLog(LOG_INFO, "GEOMETRY: [%s] Imported %d meshes", objPath, (int)objects.size());
    return true;
}

// ---------------------------------------------------------------------------------------------

static float Quantize(float value)
{
    return (float)(std::round((double)value*10000.0)/10000.0);
}

void GeoQuantize(Geometry &geometry)
{
    auto vec3 = [](Vector3 &v) { v = { Quantize(v.x), Quantize(v.y), Quantize(v.z) }; };
    for (GeoShape &shape : geometry.shapes)
    {
        for (Vector3 &v : shape.vertices) vec3(v);
        for (GeoFace &face : shape.faces)
        {
            vec3(face.uAxis);
            vec3(face.vAxis);
            face.offset = { Quantize(face.offset.x), Quantize(face.offset.y) };
            face.scale = { Quantize(face.scale.x), Quantize(face.scale.y) };
        }
    }
    for (GeoMesh &mesh : geometry.meshes)
    {
        vec3(mesh.position);
        mesh.rotation = { Quantize(mesh.rotation.x), Quantize(mesh.rotation.y), Quantize(mesh.rotation.z), Quantize(mesh.rotation.w) };
    }
}
