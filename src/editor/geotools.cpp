#include "geotools.h"
#include "raymath.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <tuple>

static const float SAME_POINT = 0.01f;  // World units: closer than this is the same point, or on a plane

// ---------------------------------------------------------------------------------------------
// Helpers

static std::vector<Vector3> FacePoints(const GeoShape &shape, const GeoFace &face)
{
    std::vector<Vector3> points;
    for (int corner : face.corners) points.push_back(shape.vertices[corner]);
    return points;
}

static Vector3 Centroid(const std::vector<Vector3> &points)
{
    Vector3 sum = { 0.0f, 0.0f, 0.0f };
    for (Vector3 p : points) sum = Vector3Add(sum, p);
    return points.empty() ? sum : Vector3Scale(sum, 1.0f/(float)points.size());
}

// The index of a vertex at this position, adding it if there isn't one
static int FindOrAddVertex(GeoShape &shape, Vector3 p)
{
    for (size_t i = 0; i < shape.vertices.size(); i++)
    {
        if (Vector3Distance(shape.vertices[i], p) < SAME_POINT) return (int)i;
    }
    shape.vertices.push_back(p);
    return (int)shape.vertices.size() - 1;
}

// Winds a face so its normal points along outward
static void FaceOutward(const GeoShape &shape, GeoFace &face, Vector3 outward)
{
    if (Vector3DotProduct(GeoPolygonNormal(FacePoints(shape, face)), outward) < 0.0f) std::reverse(face.corners.begin(), face.corners.end());
}

// A face with the template's material, world aligned for its normal
static GeoFace NewFace(const GeoFace &templateFace, const std::vector<int> &corners)
{
    GeoFace face = templateFace;
    face.corners = corners;
    return face;
}

static void RemoveUnusedVertices(GeoShape &shape)
{
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

// The face with the edge from a to b, going that way round, or -1
static int FaceWithEdge(const GeoShape &shape, int a, int b, int skip = -1)
{
    for (size_t f = 0; f < shape.faces.size(); f++)
    {
        if ((int)f == skip) continue;
        const std::vector<int> &c = shape.faces[f].corners;
        for (size_t k = 0; k < c.size(); k++)
        {
            if (c[k] == a && c[(k + 1)%c.size()] == b) return (int)f;
        }
    }
    return -1;
}

void GeoFixBentFaces(GeoShape &shape)
{
    std::vector<GeoFace> faces;
    for (const GeoFace &face : shape.faces)
    {
        std::vector<Vector3> points = FacePoints(shape, face);
        Vector3 normal = GeoPolygonNormal(points);
        float dist = Vector3DotProduct(normal, Centroid(points));
        bool flat = true;
        for (Vector3 p : points) flat = flat && fabsf(Vector3DotProduct(normal, p) - dist) < SAME_POINT;
        if (flat || points.size() <= 3)
        {
            faces.push_back(face);
            continue;
        }
        std::vector<int> triangles = GeoTriangulate(points, normal);
        for (size_t t = 0; t + 2 < triangles.size(); t += 3)
        {
            faces.push_back(NewFace(face, { face.corners[triangles[t]], face.corners[triangles[t + 1]], face.corners[triangles[t + 2]] }));
        }
    }
    shape.faces = faces;
}

// ---------------------------------------------------------------------------------------------
// Meshes

int GeoAddBox(Geometry &geometry, Vector3 corner, Vector3 u, Vector3 v, Vector3 normal, float sizeU, float sizeV, float depth,
              const std::string &material)
{
    Vector3 points[8];
    for (int i = 0; i < 8; i++)
    {
        points[i] = Vector3Add(Vector3Add(Vector3Add(corner, Vector3Scale(u, (i & 1) ? sizeU : 0.0f)),
                                          Vector3Scale(v, (i & 2) ? sizeV : 0.0f)), Vector3Scale(normal, (i & 4) ? depth : 0.0f));
    }
    Vector3 center = Centroid(std::vector<Vector3>(points, points + 8));

    GeoShape shape;
    for (Vector3 p : points) shape.vertices.push_back(Vector3Subtract(p, center));
    const int quads[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
    for (const auto &quad : quads)
    {
        GeoFace face;
        face.corners = { quad[0], quad[1], quad[2], quad[3] };
        face.material = material;
        std::vector<Vector3> facePoints = FacePoints(shape, face);
        FaceOutward(shape, face, Centroid(facePoints));  // The shape's centre is its origin
        GeoResetTexture(face, GeoPolygonNormal(FacePoints(shape, face)));
        shape.faces.push_back(face);
    }

    geometry.shapes.push_back(shape);
    geometry.meshes.push_back({ (int)geometry.shapes.size() - 1, center, { 0.0f, 0.0f, 0.0f, 1.0f } });
    return (int)geometry.meshes.size() - 1;
}

int GeoDuplicateMesh(Geometry &geometry, int mesh)
{
    geometry.meshes.push_back(geometry.meshes[mesh]);
    return (int)geometry.meshes.size() - 1;
}

void GeoDeleteMesh(Geometry &geometry, int mesh)
{
    int shape = geometry.meshes[mesh].shape;
    geometry.meshes.erase(geometry.meshes.begin() + mesh);
    for (const GeoMesh &other : geometry.meshes) if (other.shape == shape) return;  // Still used

    geometry.shapes.erase(geometry.shapes.begin() + shape);
    for (GeoMesh &other : geometry.meshes) if (other.shape > shape) other.shape--;
}

void GeoRotateMesh(Geometry &geometry, int mesh, Vector3 axis, float degrees)
{
    Quaternion turn = QuaternionFromAxisAngle(Vector3Normalize(axis), degrees*DEG2RAD);
    geometry.meshes[mesh].rotation = QuaternionNormalize(QuaternionMultiply(turn, geometry.meshes[mesh].rotation));
}

// ---------------------------------------------------------------------------------------------
// Faces and vertices

void GeoMoveFace(GeoShape &shape, int face, float distance)
{
    Vector3 normal = GeoPolygonNormal(FacePoints(shape, shape.faces[face]));
    for (int corner : shape.faces[face].corners) shape.vertices[corner] = Vector3Add(shape.vertices[corner], Vector3Scale(normal, distance));
    GeoFixBentFaces(shape);
}

void GeoMoveVertices(GeoShape &shape, const std::vector<int> &vertices, Vector3 offset)
{
    std::set<int> unique(vertices.begin(), vertices.end());
    for (int v : unique) shape.vertices[v] = Vector3Add(shape.vertices[v], offset);
    GeoFixBentFaces(shape);
}

bool GeoCutFace(GeoShape &shape, int faceIndex, Vector3 point, Vector3 direction)
{
    GeoFace face = shape.faces[faceIndex];
    Vector3 normal = GeoPolygonNormal(FacePoints(shape, face));
    Vector3 cutNormal = Vector3CrossProduct(normal, direction);
    if (Vector3LengthSqr(cutNormal) < 1e-8f) return false;
    cutNormal = Vector3Normalize(cutNormal);
    float cutDist = Vector3DotProduct(cutNormal, point);

    // Each corner on one side or the other, with new corners where edges cross the line
    std::vector<int> front, back;
    size_t count = face.corners.size();
    std::vector<std::tuple<int, int, int>> inserted;  // Edge from, edge to, the new corner on it
    for (size_t k = 0; k < count; k++)
    {
        int ia = face.corners[k], ib = face.corners[(k + 1)%count];
        Vector3 a = shape.vertices[ia], b = shape.vertices[ib];
        float da = Vector3DotProduct(cutNormal, a) - cutDist, db = Vector3DotProduct(cutNormal, b) - cutDist;
        if (da >= -SAME_POINT) front.push_back(ia);
        if (da <= SAME_POINT) back.push_back(ia);
        if ((da > SAME_POINT && db < -SAME_POINT) || (da < -SAME_POINT && db > SAME_POINT))
        {
            int cut = FindOrAddVertex(shape, Vector3Lerp(a, b, da/(da - db)));
            front.push_back(cut);
            back.push_back(cut);
            inserted.push_back({ ia, ib, cut });
        }
    }
    if (front.size() < 3 || back.size() < 3 || inserted.empty()) return false;

    // The two halves, then the new corners added to the faces on the other side of the cut edges
    shape.faces[faceIndex] = NewFace(face, front);
    shape.faces.push_back(NewFace(face, back));
    for (const auto &[a, b, cut] : inserted)
    {
        int neighbour = FaceWithEdge(shape, b, a);
        if (neighbour < 0) continue;
        std::vector<int> &c = shape.faces[neighbour].corners;
        for (size_t k = 0; k < c.size(); k++)
        {
            if (c[k] == b && c[(k + 1)%c.size()] == a)
            {
                c.insert(c.begin() + k + 1, cut);
                break;
            }
        }
    }
    return true;
}

// Joins two rings of corners (the same face, before and after a step of an extrude) with a face per edge,
// facing away from the middle of the ring (or towards it, for a step into the shape)
static void JoinRings(GeoShape &shape, const GeoFace &templateFace, const std::vector<int> &from, const std::vector<int> &to, bool inward)
{
    std::vector<Vector3> ring;
    for (int i : from) ring.push_back(shape.vertices[i]);
    for (int i : to) ring.push_back(shape.vertices[i]);
    Vector3 middle = Centroid(ring);
    for (size_t k = 0; k < from.size(); k++)
    {
        size_t next = (k + 1)%from.size();
        GeoFace side = NewFace(templateFace, { from[k], from[next], to[next], to[k] });
        std::vector<Vector3> points = FacePoints(shape, side);
        Vector3 outward = Vector3Subtract(Centroid(points), middle);
        if (inward) outward = Vector3Negate(outward);
        FaceOutward(shape, side, outward);
        GeoResetTexture(side, GeoPolygonNormal(FacePoints(shape, side)));
        shape.faces.push_back(side);
    }
}

void GeoExtrudeFace(GeoShape &shape, int faceIndex, float distance)
{
    if (fabsf(distance) < SAME_POINT) return;
    GeoFace face = shape.faces[faceIndex];
    Vector3 normal = GeoPolygonNormal(FacePoints(shape, face));

    std::vector<int> moved;
    for (int corner : face.corners)
    {
        shape.vertices.push_back(Vector3Add(shape.vertices[corner], Vector3Scale(normal, distance)));
        moved.push_back((int)shape.vertices.size() - 1);
    }
    shape.faces[faceIndex].corners = moved;
    JoinRings(shape, face, face.corners, moved, distance < 0.0f);
}

void GeoExtrudeFaceAround(GeoShape &shape, int faceIndex, Vector3 pivot, Vector3 axis, float degrees, int segments)
{
    if (fabsf(degrees) < 0.01f || segments < 1) return;
    GeoFace face = shape.faces[faceIndex];
    Vector3 normal = GeoPolygonNormal(FacePoints(shape, face));
    axis = Vector3Normalize(axis);

    // Which way round is "out": the turn should carry the face forwards, along its normal
    Vector3 center = Centroid(FacePoints(shape, face));
    Vector3 swing = Vector3CrossProduct(axis, Vector3Subtract(center, pivot));
    float sign = (Vector3DotProduct(swing, normal) >= 0.0f) ? 1.0f : -1.0f;

    std::vector<int> previous = face.corners;
    for (int step = 1; step <= segments; step++)
    {
        float angle = sign*degrees*DEG2RAD*(float)step/(float)segments;
        std::vector<int> ring;
        for (int corner : face.corners)
        {
            Vector3 offset = Vector3Subtract(shape.vertices[corner], pivot);
            shape.vertices.push_back(Vector3Add(pivot, Vector3RotateByAxisAngle(offset, axis, angle)));
            ring.push_back((int)shape.vertices.size() - 1);
        }
        JoinRings(shape, face, previous, ring, false);
        previous = ring;
    }
    shape.faces[faceIndex].corners = previous;
    GeoFixBentFaces(shape);
}

// ---------------------------------------------------------------------------------------------
// Push/pull: cutting with CSG, the way csg.js does it, with BSP trees of the polygons

struct CsgPolygon
{
    std::vector<Vector3> points;
    Vector3 normal;
    float dist;
    int source;  // Index of the face template it came from
};

static CsgPolygon MakeCsgPolygon(std::vector<Vector3> points, int source)
{
    CsgPolygon polygon;
    polygon.points = points;
    polygon.normal = GeoPolygonNormal(points);
    polygon.dist = Vector3DotProduct(polygon.normal, points[0]);
    polygon.source = source;
    return polygon;
}

static void FlipPolygon(CsgPolygon &polygon)
{
    std::reverse(polygon.points.begin(), polygon.points.end());
    polygon.normal = Vector3Negate(polygon.normal);
    polygon.dist = -polygon.dist;
}

static const float CSG_EPSILON = 0.001f;

static void SplitCsgPolygon(const CsgPolygon &polygon, Vector3 normal, float dist, std::vector<CsgPolygon> &coplanarFront,
                            std::vector<CsgPolygon> &coplanarBack, std::vector<CsgPolygon> &front, std::vector<CsgPolygon> &back)
{
    enum { COPLANAR = 0, FRONT = 1, BACK = 2, SPANNING = 3 };
    int polygonType = 0;
    std::vector<int> types;
    for (Vector3 p : polygon.points)
    {
        float t = Vector3DotProduct(normal, p) - dist;
        int type = (t < -CSG_EPSILON) ? BACK : (t > CSG_EPSILON) ? FRONT : COPLANAR;
        polygonType |= type;
        types.push_back(type);
    }
    switch (polygonType)
    {
        case COPLANAR: (Vector3DotProduct(normal, polygon.normal) > 0.0f ? coplanarFront : coplanarBack).push_back(polygon); break;
        case FRONT: front.push_back(polygon); break;
        case BACK: back.push_back(polygon); break;
        default:
        {
            std::vector<Vector3> f, b;
            for (size_t i = 0; i < polygon.points.size(); i++)
            {
                size_t j = (i + 1)%polygon.points.size();
                int ti = types[i], tj = types[j];
                Vector3 vi = polygon.points[i], vj = polygon.points[j];
                if (ti != BACK) f.push_back(vi);
                if (ti != FRONT) b.push_back(vi);
                if ((ti | tj) == SPANNING)
                {
                    float t = (dist - Vector3DotProduct(normal, vi))/Vector3DotProduct(normal, Vector3Subtract(vj, vi));
                    Vector3 v = Vector3Lerp(vi, vj, t);
                    f.push_back(v);
                    b.push_back(v);
                }
            }
            if (f.size() >= 3) { CsgPolygon p = polygon; p.points = f; front.push_back(p); }
            if (b.size() >= 3) { CsgPolygon p = polygon; p.points = b; back.push_back(p); }
        }
    }
}

struct CsgNode
{
    bool hasPlane = false;
    Vector3 normal = { 0.0f, 0.0f, 0.0f };
    float dist = 0.0f;
    std::unique_ptr<CsgNode> front, back;
    std::vector<CsgPolygon> polygons;

    void Build(const std::vector<CsgPolygon> &list)
    {
        if (list.empty()) return;
        if (!hasPlane)
        {
            hasPlane = true;
            normal = list[0].normal;
            dist = list[0].dist;
        }
        std::vector<CsgPolygon> f, b;
        for (const CsgPolygon &polygon : list) SplitCsgPolygon(polygon, normal, dist, polygons, polygons, f, b);
        if (!f.empty())
        {
            if (!front) front = std::make_unique<CsgNode>();
            front->Build(f);
        }
        if (!b.empty())
        {
            if (!back) back = std::make_unique<CsgNode>();
            back->Build(b);
        }
    }

    void Invert()
    {
        for (CsgPolygon &polygon : polygons) FlipPolygon(polygon);
        normal = Vector3Negate(normal);
        dist = -dist;
        if (front) front->Invert();
        if (back) back->Invert();
        std::swap(front, back);
    }

    std::vector<CsgPolygon> ClipPolygons(const std::vector<CsgPolygon> &list) const
    {
        if (!hasPlane) return list;
        std::vector<CsgPolygon> f, b;
        for (const CsgPolygon &polygon : list) SplitCsgPolygon(polygon, normal, dist, f, b, f, b);
        if (front) f = front->ClipPolygons(f);
        if (back) b = back->ClipPolygons(b);
        else b.clear();
        f.insert(f.end(), b.begin(), b.end());
        return f;
    }

    void ClipTo(const CsgNode &bsp)
    {
        polygons = bsp.ClipPolygons(polygons);
        if (front) front->ClipTo(bsp);
        if (back) back->ClipTo(bsp);
    }

    void AllPolygons(std::vector<CsgPolygon> &out) const
    {
        out.insert(out.end(), polygons.begin(), polygons.end());
        if (front) front->AllPolygons(out);
        if (back) back->AllPolygons(out);
    }
};

// What's left of a once b is cut out of it
static std::vector<CsgPolygon> CsgSubtract(const std::vector<CsgPolygon> &aPolygons, const std::vector<CsgPolygon> &bPolygons)
{
    CsgNode a, b;
    a.Build(aPolygons);
    b.Build(bPolygons);
    a.Invert();
    a.ClipTo(b);
    b.ClipTo(a);
    b.Invert();
    b.ClipTo(a);
    b.Invert();
    std::vector<CsgPolygon> bLeft;
    b.AllPolygons(bLeft);
    a.Build(bLeft);
    a.Invert();
    std::vector<CsgPolygon> result;
    a.AllPolygons(result);
    return result;
}

// Puts corners on face edges where another face's corner lies along them (a T junction), so faces that
// meet share their edges exactly, which tools that find a face's neighbours rely on
static void FixTJunctions(GeoShape &shape)
{
    for (GeoFace &face : shape.faces)
    {
        std::vector<int> corners;
        size_t count = face.corners.size();
        for (size_t k = 0; k < count; k++)
        {
            int ia = face.corners[k], ib = face.corners[(k + 1)%count];
            Vector3 a = shape.vertices[ia], b = shape.vertices[ib];
            corners.push_back(ia);

            // Vertices strictly between a and b, nearest a first
            Vector3 ab = Vector3Subtract(b, a);
            float length = Vector3Length(ab);
            if (length < SAME_POINT) continue;
            Vector3 dir = Vector3Scale(ab, 1.0f/length);
            std::vector<std::pair<float, int>> between;
            for (size_t v = 0; v < shape.vertices.size(); v++)
            {
                if ((int)v == ia || (int)v == ib) continue;
                Vector3 ap = Vector3Subtract(shape.vertices[v], a);
                float along = Vector3DotProduct(ap, dir);
                if (along <= SAME_POINT || along >= length - SAME_POINT) continue;
                if (Vector3Length(Vector3Subtract(ap, Vector3Scale(dir, along))) < SAME_POINT) between.push_back({ along, (int)v });
            }
            std::sort(between.begin(), between.end());
            for (const auto &[along, v] : between) corners.push_back(v);
        }
        face.corners = corners;
    }
}

void GeoPushPull(Geometry &geometry, int mesh, int faceIndex, float distance)
{
    GeoShape &shape = geometry.shapes[geometry.meshes[mesh].shape];
    if (distance >= 0.0f)
    {
        GeoExtrudeFace(shape, faceIndex, distance);
        return;
    }

    // The shape's faces, then the face's outline swept into it (a prism), starting a little outside it so
    // nothing lies exactly on the shape's surface
    GeoFace pushed = shape.faces[faceIndex];
    std::vector<Vector3> outline = FacePoints(shape, pushed);
    Vector3 normal = GeoPolygonNormal(outline);
    std::vector<GeoFace> templates = shape.faces;
    std::vector<CsgPolygon> solid;
    for (size_t f = 0; f < shape.faces.size(); f++) solid.push_back(MakeCsgPolygon(FacePoints(shape, shape.faces[f]), (int)f));

    std::vector<Vector3> top, bottom;
    for (Vector3 p : outline)
    {
        top.push_back(Vector3Add(p, Vector3Scale(normal, 0.5f)));
        bottom.push_back(Vector3Add(p, Vector3Scale(normal, distance)));
    }
    int cutSource = (int)templates.size();
    templates.push_back(pushed);
    std::vector<CsgPolygon> prism;
    prism.push_back(MakeCsgPolygon(top, cutSource));
    std::vector<Vector3> bottomReversed(bottom.rbegin(), bottom.rend());
    prism.push_back(MakeCsgPolygon(bottomReversed, cutSource));
    Vector3 prismMiddle = Vector3Scale(Vector3Add(Centroid(top), Centroid(bottom)), 0.5f);
    for (size_t k = 0; k < outline.size(); k++)
    {
        size_t next = (k + 1)%outline.size();
        std::vector<Vector3> side = { top[k], top[next], bottom[next], bottom[k] };
        if (Vector3DotProduct(GeoPolygonNormal(side), Vector3Subtract(Centroid(side), prismMiddle)) < 0.0f) std::reverse(side.begin(), side.end());
        prism.push_back(MakeCsgPolygon(side, cutSource));
    }

    std::vector<CsgPolygon> result = CsgSubtract(solid, prism);

    // Back into a shape: shared corners joined, the cut's faces world aligned for whichever way they face
    Quaternion rotation = geometry.meshes[mesh].rotation;
    GeoShape cut;
    for (const CsgPolygon &polygon : result)
    {
        GeoFace face = templates[polygon.source];
        face.corners.clear();
        for (Vector3 p : polygon.points)
        {
            int v = FindOrAddVertex(cut, p);
            if (face.corners.empty() || face.corners.back() != v) face.corners.push_back(v);
        }
        while (face.corners.size() > 1 && face.corners.front() == face.corners.back()) face.corners.pop_back();
        if (face.corners.size() < 3) continue;
        if (polygon.source == cutSource) GeoResetTexture(face, Vector3RotateByQuaternion(polygon.normal, rotation));
        cut.faces.push_back(face);
    }
    FixTJunctions(cut);
    GeoMergeFaces(cut);
    shape = cut;
}

// ---------------------------------------------------------------------------------------------
// Bevel

bool GeoBevelEdges(GeoShape &shape, const std::vector<std::pair<int, int>> &edges, float &width, int segments, std::string &error)
{
    if (width <= 0.0f || segments < 1) return true;
    segments = std::min(segments, 32);

    struct BevelPlan
    {
        int a, b, f1, f2, a1, a2, b1, b2;
    };
    std::vector<BevelPlan> plans;
    std::set<int> ends;
    float widest = FLT_MAX;
    for (const auto &[a, b] : edges)
    {
        if (ends.count(a) || ends.count(b))
        {
            error = "Two of the selected edges share a corner, which bevel can't do yet";
            return false;
        }
        ends.insert(a);
        ends.insert(b);

        BevelPlan plan = { a, b, FaceWithEdge(shape, a, b), FaceWithEdge(shape, b, a), -1, -1, -1, -1 };
        if (plan.f1 < 0 || plan.f2 < 0)
        {
            error = "An edge isn't between two faces";
            return false;
        }
        for (int corner : { a, b })
        {
            int faces = 0;
            for (const GeoFace &face : shape.faces) faces += (int)std::count(face.corners.begin(), face.corners.end(), corner);
            if (faces != 3)
            {
                error = "Bevel needs exactly three faces meeting at each end of an edge, like a box's corners";
                return false;
            }
        }

        // The corners next to each end in the two faces: f1 runs a to b, f2 runs b to a
        auto neighbour = [&](int face, int corner, int step)
        {
            const std::vector<int> &c = shape.faces[face].corners;
            int k = (int)(std::find(c.begin(), c.end(), corner) - c.begin());
            return c[(k + step + (int)c.size())%c.size()];
        };
        plan.a1 = neighbour(plan.f1, a, -1);
        plan.b1 = neighbour(plan.f1, b, 1);
        plan.a2 = neighbour(plan.f2, a, 1);
        plan.b2 = neighbour(plan.f2, b, -1);
        // No wider than the faces next to the edge allow
        for (auto [end, other] : { std::pair<int, int>{ a, plan.a1 }, { a, plan.a2 }, { b, plan.b1 }, { b, plan.b2 } })
        {
            widest = fminf(widest, Vector3Distance(shape.vertices[end], shape.vertices[other]) - 0.5f);
        }
        plans.push_back(plan);
    }
    if (widest <= 0.0f)
    {
        error = "The faces next to the edge are too small to bevel";
        return false;
    }
    width = fminf(width, widest);

    for (const BevelPlan &plan : plans)
    {
        // The rounded profile at each end: from the point on the edge to x1, round to the point on the edge to
        // x2, a curve pulled towards the old corner
        auto profile = [&](int end, int towards1, int towards2)
        {
            Vector3 corner = shape.vertices[end];
            Vector3 p1 = Vector3Add(corner, Vector3Scale(Vector3Normalize(Vector3Subtract(shape.vertices[towards1], corner)), width));
            Vector3 p2 = Vector3Add(corner, Vector3Scale(Vector3Normalize(Vector3Subtract(shape.vertices[towards2], corner)), width));
            std::vector<int> points;
            for (int k = 0; k <= segments; k++)
            {
                float t = (float)k/(float)segments;
                Vector3 p = Vector3Add(Vector3Add(Vector3Scale(p1, (1 - t)*(1 - t)), Vector3Scale(corner, 2*t*(1 - t))), Vector3Scale(p2, t*t));
                shape.vertices.push_back(p);
                points.push_back((int)shape.vertices.size() - 1);
            }
            return points;
        };
        std::vector<int> profileA = profile(plan.a, plan.a1, plan.a2);
        std::vector<int> profileB = profile(plan.b, plan.b1, plan.b2);

        Vector3 outward = Vector3Add(GeoPolygonNormal(FacePoints(shape, shape.faces[plan.f1])), GeoPolygonNormal(FacePoints(shape, shape.faces[plan.f2])));
        GeoFace templateFace = shape.faces[plan.f1];

        // Each face at an end swaps that corner for its part of the profile
        for (size_t f = 0; f < shape.faces.size(); f++)
        {
            std::vector<int> &c = shape.faces[f].corners;
            for (int end : { plan.a, plan.b })
            {
                auto it = std::find(c.begin(), c.end(), end);
                if (it == c.end()) continue;
                const std::vector<int> &prof = (end == plan.a) ? profileA : profileB;
                int other = (end == plan.a) ? plan.b : plan.a;
                int n1 = (end == plan.a) ? plan.a1 : plan.b1;
                if ((int)f == plan.f1) *it = prof.front();
                else if ((int)f == plan.f2) *it = prof.back();
                else
                {
                    // The third face: the profile runs from its corner next to n1 round to the other one
                    size_t k = it - c.begin();
                    int previous = c[(k + c.size() - 1)%c.size()];
                    std::vector<int> run = prof;
                    if (previous != n1) std::reverse(run.begin(), run.end());
                    c.erase(c.begin() + k);
                    c.insert(c.begin() + k, run.begin(), run.end());
                }
                (void)other;
            }
        }

        // The rounded strip along the edge
        for (int k = 0; k < segments; k++)
        {
            GeoFace strip = NewFace(templateFace, { profileA[k], profileB[k], profileB[k + 1], profileA[k + 1] });
            FaceOutward(shape, strip, outward);
            GeoResetTexture(strip, GeoPolygonNormal(FacePoints(shape, strip)));
            shape.faces.push_back(strip);
        }
    }
    RemoveUnusedVertices(shape);
    return true;
}
