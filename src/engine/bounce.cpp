#include "bounce.h"
#include "gamedata.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <map>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

static const int DIRECTION_SETS = 2;   // Turned copies of the 26 directions each texel traces
static const int SMOOTH_RADIUS = 1;    // Texels around each one it's averaged with after baking. Keep under PAD + 1
static const int PAD = 2;                    // Texels of border around each chart, filled from its edge, so filtering never reads the next chart
static const int MAX_LIGHTMAP_SIZE = 4096;
static const float TRACE_DISTANCE = 64.0f;   // How far a bounce trace looks for a surface
static const float SURFACE_OFFSET = 0.02f;   // Traces start this far off a surface, so they don't hit it
static const float MAX_ALBEDO = 0.9f;        // No surface bounces all the light that hits it

// The lightmap is 8 bits per channel, storing bounce/BOUNCE_RANGE so it can go above 1. The lighting
// shader multiplies by the same number
static const float BOUNCE_RANGE = 2.0f;

// Part of every lightmap key, so changing how the bake works makes old lightmaps out of date
// 2: directions turned per texel, two sets, smoothed. 3: four layers, for bump maps
static const unsigned long long BAKE_VERSION = 3;

// Half-Life 2's three directions for bump mapped lighting, in the texture's own space (x and y along the
// texture, z off the surface): 55 degrees off the surface, 120 degrees apart. The lighting shader has them too
static const Vector3 BUMP_BASIS[3] = {
    { 0.81649658f, 0.0f, 0.57735027f },           // sqrt(2/3), 0, 1/sqrt(3)
    { -0.40824829f, 0.70710678f, 0.57735027f },   // -1/sqrt(6), 1/sqrt(2), 1/sqrt(3)
    { -0.40824829f, -0.70710678f, 0.57735027f },  // -1/sqrt(6), -1/sqrt(2), 1/sqrt(3)
};

// The three directions add up to sqrt(3) times the surface's normal, so for light all three face, their
// average is the head on bounce divided by sqrt(3). This makes up for it, so a flat bump map (which blends
// the three equally) comes out as bright as no bump map
static const float BUMP_BASIS_SCALE = 1.73205081f;

// FNV-1a, a quick hash for lightmap keys
static void Hash(unsigned long long &hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

// The 26 directions: every combination of -1, 0 and 1 on each axis except 0 0 0. That's the 6 axes, the
// 12 edge diagonals and the 8 corner diagonals
static std::vector<Vector3> TraceDirections()
{
    std::vector<Vector3> directions;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            for (int z = -1; z <= 1; z++)
                if (x != 0 || y != 0 || z != 0) directions.push_back(Vector3Normalize({ (float)x, (float)y, (float)z }));
    return directions;
}

// ---------------------------------------------------------------------------------------------
// Tracing. Only reads its inputs, so it's safe on the bake thread

// Möller-Trumbore, both sides. Returns the distance along the ray, or -1 for no hit
static float RayTriangle(Vector3 origin, Vector3 dir, const BounceTriangle &tri)
{
    Vector3 e1 = Vector3Subtract(tri.b, tri.a);
    Vector3 e2 = Vector3Subtract(tri.c, tri.a);
    Vector3 p = Vector3CrossProduct(dir, e2);
    float det = Vector3DotProduct(e1, p);
    if (fabsf(det) < 1e-8f) return -1.0f;

    float inv = 1.0f/det;
    Vector3 s = Vector3Subtract(origin, tri.a);
    float u = Vector3DotProduct(s, p)*inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    Vector3 q = Vector3CrossProduct(s, e1);
    float v = Vector3DotProduct(dir, q)*inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    float t = Vector3DotProduct(e2, q)*inv;
    return (t > 0.0f) ? t : -1.0f;
}

// The nearest triangle a ray hits within TRACE_DISTANCE, or -1
static int TraceNearest(const std::vector<BounceTriangle> &triangles, Vector3 origin, Vector3 dir, float &distance)
{
    int nearest = -1;
    distance = TRACE_DISTANCE;
    for (size_t i = 0; i < triangles.size(); i++)
    {
        float t = RayTriangle(origin, dir, triangles[i]);
        if (t > 0.0f && t < distance)
        {
            distance = t;
            nearest = (int)i;
        }
    }
    return nearest;
}

// Whether the sun reaches a point: a line trace towards it through the collision brushes
static bool SunReaches(const World &world, Vector3 point, Vector3 towardSun)
{
    float far = Vector3Length(Vector3Subtract(world.bounds.max, world.bounds.min)) + 1.0f;
    Trace trace = TraceBox(world, point, Vector3Add(point, Vector3Scale(towardSun, far)), Vector3Zero(), Vector3Zero());
    return trace.fraction >= 1.0f && !trace.startSolid;
}

// ---------------------------------------------------------------------------------------------
// Layout

// A texture's average color, as 0 to 1
static Vector3 AverageColor(Texture texture, Color tint)
{
    Vector3 sum = { 1.0f, 1.0f, 1.0f };
    Image image = LoadImageFromTexture(texture);
    if (image.data != nullptr)
    {
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const Color *pixels = (const Color *)image.data;
        int count = image.width*image.height;
        sum = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < count; i++) sum = Vector3Add(sum, { pixels[i].r/255.0f, pixels[i].g/255.0f, pixels[i].b/255.0f });
        sum = Vector3Scale(sum, 1.0f/(float)count);
        UnloadImage(image);
    }
    Vector3 tinted = { sum.x*tint.r/255.0f, sum.y*tint.g/255.0f, sum.z*tint.b/255.0f };
    return Vector3Min(tinted, { MAX_ALBEDO, MAX_ALBEDO, MAX_ALBEDO });
}

// What gets a part of the lightmap: the world model, and each static prop, placed by its transform
struct LayoutSource
{
    const Model *model;
    Matrix transform;
    int prop;  // Index into world.props, -1 for the world model
};

// A triangle while laying out: which source, mesh and vertex it starts at, and its corners in world space
struct LayoutTriangle
{
    int source, mesh, vertex;
    Vector3 p[3];
    Vector2 uv[3];   // Its texture coordinates, for which way the texture runs across it
    Vector3 normal;
    float dist;  // dot(normal, p) for points on its plane
};

// Union-find, to join triangles that share a corner into charts
static int FindRoot(std::vector<int> &parent, int i)
{
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
}

// A flat group of connected triangles, and its rectangle in the lightmap
struct Chart
{
    std::vector<int> triangles;
    Vector3 normal, u, v;   // u and v run along the plane: lightmap x and y
    Vector3 tangent, bitangent;  // Which ways the texture's x and y run along it, see TextureDirections
    float dist;
    float minU, minV;       // The triangles' extent along u and v, in world units
    int width, height;      // In texels, padding included
    int x, y;               // Where it is in the lightmap
};

// Whether a point on the chart's plane (in u, v) is on one of its triangles, or within `margin` of one
static bool OnChart(const Chart &chart, const std::vector<LayoutTriangle> &tris, float pu, float pv, float margin)
{
    for (int t : chart.triangles)
    {
        Vector2 a = { Vector3DotProduct(tris[t].p[0], chart.u), Vector3DotProduct(tris[t].p[0], chart.v) };
        Vector2 b = { Vector3DotProduct(tris[t].p[1], chart.u), Vector3DotProduct(tris[t].p[1], chart.v) };
        Vector2 c = { Vector3DotProduct(tris[t].p[2], chart.u), Vector3DotProduct(tris[t].p[2], chart.v) };
        float area = (b.x - a.x)*(c.y - a.y) - (b.y - a.y)*(c.x - a.x);
        if (fabsf(area) < 1e-10f) continue;
        float sign = (area > 0.0f) ? 1.0f : -1.0f;

        // Each edge's signed distance to the point, positive inside
        auto edge = [&](Vector2 p0, Vector2 p1)
        {
            float length = Vector2Distance(p0, p1);
            if (length < 1e-6f) return FLT_MAX;
            return sign*((p1.x - p0.x)*(pv - p0.y) - (p1.y - p0.y)*(pu - p0.x))/length;
        };
        if (edge(a, b) >= -margin && edge(b, c) >= -margin && edge(c, a) >= -margin) return true;
    }
    return false;
}

// Which ways the chart's texture x and y run along it, from the first of its triangles whose texture
// coordinates aren't squashed flat. The same directions the lighting shader works out per pixel to read bump
// maps, so the bounce from each of its three directions lines up with them. Square to the normal
static void TextureDirections(Chart &chart, const std::vector<LayoutTriangle> &tris)
{
    chart.tangent = chart.u;
    chart.bitangent = chart.v;
    for (int t : chart.triangles)
    {
        const LayoutTriangle &tri = tris[t];
        Vector3 e1 = Vector3Subtract(tri.p[1], tri.p[0]), e2 = Vector3Subtract(tri.p[2], tri.p[0]);
        Vector2 d1 = Vector2Subtract(tri.uv[1], tri.uv[0]), d2 = Vector2Subtract(tri.uv[2], tri.uv[0]);
        float det = d1.x*d2.y - d2.x*d1.y;
        if (fabsf(det) < 1e-8f) continue;

        Vector3 tangent = Vector3Scale(Vector3Subtract(Vector3Scale(e1, d2.y), Vector3Scale(e2, d1.y)), 1.0f/det);
        Vector3 bitangent = Vector3Scale(Vector3Subtract(Vector3Scale(e2, d1.x), Vector3Scale(e1, d2.x)), 1.0f/det);
        tangent = Vector3Subtract(tangent, Vector3Scale(chart.normal, Vector3DotProduct(chart.normal, tangent)));
        if (Vector3LengthSqr(tangent) < 1e-12f) continue;
        tangent = Vector3Normalize(tangent);
        bitangent = Vector3Subtract(bitangent, Vector3Scale(chart.normal, Vector3DotProduct(chart.normal, bitangent)));
        bitangent = Vector3Subtract(bitangent, Vector3Scale(tangent, Vector3DotProduct(tangent, bitangent)));
        if (Vector3LengthSqr(bitangent) < 1e-12f) continue;
        chart.tangent = tangent;
        chart.bitangent = Vector3Normalize(bitangent);
        return;
    }
}

// The mesh again, with the lightmap texture coordinates added, since raylib only makes room for those when uploading
static Mesh WithTexcoords2(const Mesh &old, float *texcoords2)
{
    Mesh mesh = { 0 };
    mesh.vertexCount = old.vertexCount;
    mesh.triangleCount = old.triangleCount;
    size_t count = (size_t)old.vertexCount;
    mesh.vertices = (float *)RL_MALLOC(count*3*sizeof(float));  // UnloadModel frees these with RL_FREE
    memcpy(mesh.vertices, old.vertices, count*3*sizeof(float));
    if (old.texcoords)
    {
        mesh.texcoords = (float *)RL_MALLOC(count*2*sizeof(float));
        memcpy(mesh.texcoords, old.texcoords, count*2*sizeof(float));
    }
    if (old.normals)
    {
        mesh.normals = (float *)RL_MALLOC(count*3*sizeof(float));
        memcpy(mesh.normals, old.normals, count*3*sizeof(float));
    }
    mesh.texcoords2 = texcoords2;
    UploadMesh(&mesh, false);
    return mesh;
}

void BuildLightmapLayout(World &world)
{
    const float density = (world.lightmapDensity > 0.0f) ? world.lightmapDensity : LIGHTMAP_DENSITY;

    // The world model, then the static props. Other props move, so they can't have baked light
    std::vector<LayoutSource> sources = { { &world.model, world.model.transform, -1 } };
    for (size_t i = 0; i < world.props.size(); i++)
    {
        if (world.props[i].isStatic) sources.push_back({ &world.propModels[world.props[i].model].model, world.props[i].transform, (int)i });
    }

    // What each material bounces: its texture's average color. Once per model, however many props use it
    std::map<const Model *, std::vector<Vector3>> albedos;
    for (const LayoutSource &source : sources)
    {
        std::vector<Vector3> &modelAlbedos = albedos[source.model];
        if (!modelAlbedos.empty()) continue;
        modelAlbedos.resize(source.model->materialCount);
        for (int i = 0; i < source.model->materialCount; i++)
        {
            const MaterialMap &map = source.model->materials[i].maps[MATERIAL_MAP_DIFFUSE];
            modelAlbedos[i] = AverageColor(map.texture, map.color);
        }
    }

    // Every triangle in world space, also kept for tracing. The world's come first, then the props'
    std::vector<LayoutTriangle> tris;
    world.bounceTriangles.clear();
    for (size_t s = 0; s < sources.size(); s++)
    {
        const Model &model = *sources[s].model;
        if (s == 1) world.propTriangleStart = (int)world.bounceTriangles.size();
        for (int m = 0; m < model.meshCount; m++)
        {
            const Mesh &mesh = model.meshes[m];
            for (int v = 0; v + 2 < mesh.vertexCount; v += 3)
            {
                LayoutTriangle t;
                t.source = (int)s;
                t.mesh = m;
                t.vertex = v;
                for (int k = 0; k < 3; k++)
                {
                    const float *p = &mesh.vertices[(v + k)*3];
                    t.p[k] = Vector3Transform({ p[0], p[1], p[2] }, sources[s].transform);
                    t.uv[k] = mesh.texcoords ? Vector2{ mesh.texcoords[(v + k)*2], mesh.texcoords[(v + k)*2 + 1] } : Vector2{ 0.0f, 0.0f };
                }
                Vector3 cross = Vector3CrossProduct(Vector3Subtract(t.p[1], t.p[0]), Vector3Subtract(t.p[2], t.p[0]));
                if (Vector3LengthSqr(cross) < 1e-12f) continue;  // Degenerate
                t.normal = Vector3Normalize(cross);
                t.dist = Vector3DotProduct(t.normal, t.p[0]);
                tris.push_back(t);
                world.bounceTriangles.push_back({ t.p[0], t.p[1], t.p[2], t.normal, albedos[&model][model.meshMaterial[m]] });
            }
        }
    }
    if (sources.size() == 1) world.propTriangleStart = (int)world.bounceTriangles.size();

    // Charts: triangles in the same plane, joined where they share a corner
    std::vector<int> parent(tris.size());
    for (size_t i = 0; i < tris.size(); i++) parent[i] = (int)i;
    std::map<std::tuple<int, int, int, int, int, int, int>, int> seen;  // Plane and corner position -> a triangle with it
    for (size_t i = 0; i < tris.size(); i++)
    {
        const LayoutTriangle &t = tris[i];
        int nx = (int)lroundf(t.normal.x*1000.0f), ny = (int)lroundf(t.normal.y*1000.0f), nz = (int)lroundf(t.normal.z*1000.0f);
        int d = (int)lroundf(t.dist*100.0f);
        for (const Vector3 &p : t.p)
        {
            auto key = std::make_tuple(nx, ny, nz, d, (int)lroundf(p.x*1000.0f), (int)lroundf(p.y*1000.0f), (int)lroundf(p.z*1000.0f));
            auto found = seen.find(key);
            if (found == seen.end()) seen[key] = (int)i;
            else parent[FindRoot(parent, (int)i)] = FindRoot(parent, found->second);
        }
    }

    std::map<int, int> chartOf;
    std::vector<Chart> charts;
    for (size_t i = 0; i < tris.size(); i++)
    {
        int root = FindRoot(parent, (int)i);
        auto found = chartOf.find(root);
        if (found == chartOf.end())
        {
            found = chartOf.emplace(root, (int)charts.size()).first;
            charts.emplace_back();
        }
        charts[found->second].triangles.push_back((int)i);
    }

    // Each chart's directions along its plane, extent, and size in texels
    for (Chart &chart : charts)
    {
        const LayoutTriangle &first = tris[chart.triangles[0]];
        chart.normal = first.normal;
        chart.dist = first.dist;
        Vector3 reference = (fabsf(chart.normal.y) < 0.9f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
        chart.u = Vector3Normalize(Vector3CrossProduct(reference, chart.normal));
        chart.v = Vector3CrossProduct(chart.normal, chart.u);
        TextureDirections(chart, tris);

        float maxU = -FLT_MAX, maxV = -FLT_MAX;
        chart.minU = chart.minV = FLT_MAX;
        for (int t : chart.triangles)
        {
            for (const Vector3 &p : tris[t].p)
            {
                float pu = Vector3DotProduct(p, chart.u), pv = Vector3DotProduct(p, chart.v);
                chart.minU = fminf(chart.minU, pu); maxU = fmaxf(maxU, pu);
                chart.minV = fminf(chart.minV, pv); maxV = fmaxf(maxV, pv);
            }
        }
        chart.width = (int)ceilf((maxU - chart.minU)*density) + 1 + 2*PAD;
        chart.height = (int)ceilf((maxV - chart.minV)*density) + 1 + 2*PAD;
    }

    // Packed in rows, tallest first, into a power of two wide lightmap about as wide as it is tall
    std::vector<int> order(charts.size());
    long long area = 0;
    int widest = 1;
    for (size_t i = 0; i < charts.size(); i++)
    {
        order[i] = (int)i;
        area += (long long)charts[i].width*charts[i].height;
        widest = std::max(widest, charts[i].width);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return charts[a].height > charts[b].height; });

    int width = 64;
    while (width < widest || (long long)width*width < area*5/4) width *= 2;
    int x = 0, y = 0, rowHeight = 0;
    for (int i : order)
    {
        if (x + charts[i].width > width)
        {
            x = 0;
            y += rowHeight;
            rowHeight = 0;
        }
        charts[i].x = x;
        charts[i].y = y;
        x += charts[i].width;
        rowHeight = std::max(rowHeight, charts[i].height);
    }
    int height = y + rowHeight;
    if (width > MAX_LIGHTMAP_SIZE || height > MAX_LIGHTMAP_SIZE)
    {
        TraceLog(LOG_WARNING, "LIGHTMAP: The world needs a %dx%d lightmap, bigger than %d", width, height, MAX_LIGHTMAP_SIZE);
    }
    world.lightmapWidth = width;
    world.lightmapHeight = height;

    // Lightmap texture coordinates for each source's meshes, at the middle of the texels each corner lands in
    std::vector<std::vector<float *>> texcoords2(sources.size());
    for (size_t s = 0; s < sources.size(); s++)
    {
        const Model &model = *sources[s].model;
        texcoords2[s].resize(model.meshCount);
        for (int m = 0; m < model.meshCount; m++) texcoords2[s][m] = (float *)RL_CALLOC(model.meshes[m].vertexCount*2, sizeof(float));
    }
    for (const Chart &chart : charts)
    {
        for (int t : chart.triangles)
        {
            for (int k = 0; k < 3; k++)
            {
                const Vector3 &p = tris[t].p[k];
                float texelX = chart.x + PAD + 0.5f + (Vector3DotProduct(p, chart.u) - chart.minU)*density;
                float texelY = chart.y + PAD + 0.5f + (Vector3DotProduct(p, chart.v) - chart.minV)*density;
                float *uv = &texcoords2[tris[t].source][tris[t].mesh][(tris[t].vertex + k)*2];
                uv[0] = texelX/(float)width;
                uv[1] = texelY/(float)height;
            }
        }
    }

    // The world model's meshes are rebuilt with them. Each static prop gets its own copies of its model's
    // meshes, since two props of the same model sit in different parts of the lightmap
    for (int m = 0; m < world.model.meshCount; m++)
    {
        Mesh rebuilt = WithTexcoords2(world.model.meshes[m], texcoords2[0][m]);
        UnloadMesh(world.model.meshes[m]);
        world.model.meshes[m] = rebuilt;
    }
    for (size_t s = 1; s < sources.size(); s++)
    {
        WorldProp &prop = world.props[sources[s].prop];
        for (Mesh &mesh : prop.lightmapMeshes) UnloadMesh(mesh);
        prop.lightmapMeshes.clear();
        for (int m = 0; m < sources[s].model->meshCount; m++) prop.lightmapMeshes.push_back(WithTexcoords2(sources[s].model->meshes[m], texcoords2[s][m]));
    }

    // The texels that lie on the world. Ones in a chart's padding, or in a gap between its triangles, are
    // filled from their neighbours after the bake instead
    world.lightmapTexels.clear();
    const float margin = 0.75f/density;  // About half a texel's diagonal, so texels along edges count
    for (const Chart &chart : charts)
    {
        for (int j = 0; j < chart.height; j++)
        {
            for (int i = 0; i < chart.width; i++)
            {
                float pu = chart.minU + (i - PAD)/density;
                float pv = chart.minV + (j - PAD)/density;
                if (!OnChart(chart, tris, pu, pv, margin)) continue;

                Vector3 position = Vector3Add(Vector3Add(Vector3Scale(chart.u, pu), Vector3Scale(chart.v, pv)), Vector3Scale(chart.normal, chart.dist));
                world.lightmapTexels.push_back({ position, chart.normal, chart.tangent, chart.bitangent, chart.x + i, chart.y + j });
            }
        }
    }

    // The world's part of the lightmap key: its triangles, their colors and the layout. The sun is added in LightmapKey
    unsigned long long key = 14695981039346656037ull;
    Hash(key, &BAKE_VERSION, sizeof(BAKE_VERSION));
    Hash(key, &width, sizeof(width));
    Hash(key, &height, sizeof(height));
    for (const BounceTriangle &tri : world.bounceTriangles) Hash(key, &tri, sizeof(tri));
    if (density != LIGHTMAP_DENSITY) Hash(key, &density, sizeof(density));  // Only when not the default, so lightmaps saved before the setting existed still match

    // Laid out again the same, like after props were placed again where they were: the lightmap still fits
    if (key != world.lightmapGeometryKey) world.lightmapKey = 0;
    world.lightmapGeometryKey = key;

    TraceLog(LOG_INFO, "LIGHTMAP: %d triangles in %d charts (%d static props), %dx%d lightmap, %d texels on the world",
             (int)tris.size(), (int)charts.size(), (int)sources.size() - 1, width, height, (int)world.lightmapTexels.size());
}

unsigned long long LightmapKey(const World &world)
{
    const Sun &sun = world.lighting.sun;
    unsigned long long key = world.lightmapGeometryKey;
    Hash(key, &sun.position, sizeof(sun.position));
    Hash(key, &sun.direction, sizeof(sun.direction));
    Hash(key, &sun.color, sizeof(sun.color));
    Hash(key, &sun.intensity, sizeof(sun.intensity));
    return key;
}

std::string LightmapKeyToText(unsigned long long key)
{
    return TextFormat("%016llx", key);
}

// ---------------------------------------------------------------------------------------------
// Baking in the background

struct LightmapJob
{
    std::thread thread;
    std::atomic<bool> cancel{ false };
    std::atomic<bool> done{ false };

    // Copies of what the bake reads, so the world can carry on being used meanwhile
    std::vector<BounceTriangle> triangles;
    int propTriangleStart;  // Triangles from here on are static props', which the sun traces check too
    std::vector<LightmapTexel> texels;
    World traceWorld;  // Only its collision brushes and bounds, for the traces towards the sun
    Sun sun;
    int width, height;
    unsigned long long key;  // LightmapKey of the world and sun it's baking

    std::vector<Color> pixels;
    BounceStats stats;

    ~LightmapJob()
    {
        cancel = true;
        if (thread.joinable()) thread.join();
    }
};

// Fills texels nobody baked (padding, gaps) from baked neighbours, a ring at a time
static void Dilate(std::vector<Color> &pixels, std::vector<bool> &filled, int width, int height, int rings)
{
    for (int ring = 0; ring < rings; ring++)
    {
        std::vector<bool> next = filled;
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                if (filled[y*width + x]) continue;
                int r = 0, g = 0, b = 0, count = 0;
                for (int dy = -1; dy <= 1; dy++)
                {
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height || !filled[ny*width + nx]) continue;
                        const Color &c = pixels[ny*width + nx];
                        r += c.r; g += c.g; b += c.b; count++;
                    }
                }
                if (count == 0) continue;
                pixels[y*width + x] = { (unsigned char)(r/count), (unsigned char)(g/count), (unsigned char)(b/count), 255 };
                next[y*width + x] = true;
            }
        }
        filled = next;
    }
}

// A rotation of the 26 directions for one texel (and which of its sets), random but the same every bake,
// so bakes come out identical. Shoemake's method turns three random numbers into an even random rotation
static Quaternion TexelRotation(int x, int y, int set)
{
    unsigned int state = (unsigned int)x*73856093u ^ (unsigned int)y*19349663u ^ (unsigned int)(set + 1)*83492791u;
    auto next = [&state]()
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;  // xorshift
        return (float)(state & 0xFFFFFF)/16777216.0f;
    };
    next();
    float u1 = next(), u2 = next(), u3 = next();
    float a = sqrtf(1.0f - u1), b = sqrtf(u1);
    return { a*sinf(2.0f*PI*u2), a*cosf(2.0f*PI*u2), b*sinf(2.0f*PI*u3), b*cosf(2.0f*PI*u3) };
}

// Averages each baked texel with the baked texels around it, within SMOOTH_RADIUS. Only baked texels count,
// and charts are kept apart by their padding, so light never bleeds from one surface to another
static void Smooth(std::vector<Vector3> &light, const std::vector<bool> &filled, int width, int height)
{
    std::vector<Vector3> smoothed = light;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            if (!filled[(size_t)y*width + x]) continue;
            Vector3 sum = { 0.0f, 0.0f, 0.0f };
            int count = 0;
            for (int dy = -SMOOTH_RADIUS; dy <= SMOOTH_RADIUS; dy++)
            {
                for (int dx = -SMOOTH_RADIUS; dx <= SMOOTH_RADIUS; dx++)
                {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height || !filled[(size_t)ny*width + nx]) continue;
                    sum = Vector3Add(sum, light[(size_t)ny*width + nx]);
                    count++;
                }
            }
            smoothed[(size_t)y*width + x] = Vector3Scale(sum, 1.0f/(float)count);
        }
    }
    light = smoothed;
}

// Whether a static prop is in the way between a point and the sun. Props aren't collision brushes (a solid
// prop only blocks with the box around it), so this checks their triangles
static bool PropsBlockSun(const LightmapJob &job, Vector3 point, Vector3 towardSun)
{
    for (size_t i = job.propTriangleStart; i < job.triangles.size(); i++)
    {
        if (RayTriangle(point, towardSun, job.triangles[i]) > 0.0f) return true;
    }
    return false;
}

static void RunBake(LightmapJob *job)
{
    auto start = std::chrono::steady_clock::now();
    const Sun &sun = job->sun;
    Vector3 towardSun = Vector3Negate(Vector3Normalize(sun.direction));
    Vector3 sunLight = Vector3Scale({ sun.color.r/255.0f, sun.color.g/255.0f, sun.color.b/255.0f }, sun.intensity);
    std::vector<Vector3> directions = TraceDirections();

    // Each trace stands for 1/(26*DIRECTION_SETS) of all directions. Light from a surface that bounces evenly
    // (Lambert) reaches the texel as albedo*irradiance/pi from that direction, times its share of directions
    const float weight = 4.0f/(float)(directions.size()*DIRECTION_SETS);

    // Four layers: the bounce reaching the surface head on, then from each of Half-Life 2's three directions
    // (its "radiosity normal mapping" basis), which tilt 55 degrees off the surface, evenly spread around it.
    // Bump mapped materials blend those three by which way each texel's bump faces
    size_t texelCount = (size_t)job->width*job->height;
    std::vector<Vector3> light(texelCount*LIGHTMAP_LAYERS, { 0.0f, 0.0f, 0.0f });
    std::vector<bool> filled(texelCount, false);

    for (const LightmapTexel &texel : job->texels)
    {
        if (job->cancel) return;

        Vector3 basis[3];
        for (int k = 0; k < 3; k++)
        {
            basis[k] = Vector3Add(Vector3Add(Vector3Scale(texel.tangent, BUMP_BASIS[k].x), Vector3Scale(texel.bitangent, BUMP_BASIS[k].y)),
                                  Vector3Scale(texel.normal, BUMP_BASIS[k].z));
        }

        Vector3 origin = Vector3Add(texel.position, Vector3Scale(texel.normal, SURFACE_OFFSET));
        Vector3 bounce[LIGHTMAP_LAYERS] = {};
        for (int set = 0; set < DIRECTION_SETS; set++)
        for (Vector3 base : directions)
        {
            // The 26 directions turned by this texel's own rotation, so neighbouring texels don't all trace
            // the same directions (which repeats shapes across the lightmap)
            Vector3 dir = Vector3RotateByQuaternion(base, TexelRotation(texel.x, texel.y, set));
            float facing = Vector3DotProduct(texel.normal, dir);
            if (facing <= 0.0f) continue;  // Behind the surface

            float distance;
            int hit = TraceNearest(job->triangles, origin, dir, distance);
            job->stats.rays++;
            if (hit < 0) continue;  // Open sky: no bounce from it

            // The side of the hit surface facing this texel, and how much sun falls on it
            const BounceTriangle &tri = job->triangles[hit];
            Vector3 hitNormal = (Vector3DotProduct(tri.normal, dir) > 0.0f) ? Vector3Negate(tri.normal) : tri.normal;
            float sunFacing = Vector3DotProduct(hitNormal, towardSun);
            if (sunFacing <= 0.0f) continue;

            Vector3 hitPoint = Vector3Add(Vector3Add(origin, Vector3Scale(dir, distance)), Vector3Scale(hitNormal, SURFACE_OFFSET));
            job->stats.rays++;
            if (!SunReaches(job->traceWorld, hitPoint, towardSun) || PropsBlockSun(*job, hitPoint, towardSun)) continue;

            // The same ray counts for all four layers, weighed by how much it faces each one
            Vector3 reflected = Vector3Scale(Vector3Multiply(tri.albedo, Vector3Scale(sunLight, sunFacing)), weight);
            bounce[0] = Vector3Add(bounce[0], Vector3Scale(reflected, facing));
            for (int k = 0; k < 3; k++)
            {
                float basisFacing = Vector3DotProduct(basis[k], dir);
                if (basisFacing > 0.0f) bounce[1 + k] = Vector3Add(bounce[1 + k], Vector3Scale(reflected, basisFacing*BUMP_BASIS_SCALE));
            }
        }

        size_t index = (size_t)texel.y*job->width + texel.x;
        for (int layer = 0; layer < LIGHTMAP_LAYERS; layer++) light[layer*texelCount + index] = bounce[layer];
        filled[index] = true;
    }

    // Each layer is smoothed (what noise is left from the turned directions), stored in 8 bits, and filled
    // out past its charts' edges, then stacked one above the next
    auto channel = [](float v) { return (unsigned char)Clamp(v/BOUNCE_RANGE*255.0f, 0.0f, 255.0f); };
    job->pixels.clear();
    for (int layer = 0; layer < LIGHTMAP_LAYERS; layer++)
    {
        std::vector<Vector3> layerLight(light.begin() + layer*texelCount, light.begin() + (layer + 1)*texelCount);
        Smooth(layerLight, filled, job->width, job->height);

        std::vector<Color> pixels(texelCount, { 0, 0, 0, 255 });
        for (size_t i = 0; i < texelCount; i++)
        {
            if (filled[i]) pixels[i] = { channel(layerLight[i].x), channel(layerLight[i].y), channel(layerLight[i].z), 255 };
        }
        std::vector<bool> layerFilled = filled;  // Dilate fills it in as it goes
        Dilate(pixels, layerFilled, job->width, job->height, PAD + 1);
        job->pixels.insert(job->pixels.end(), pixels.begin(), pixels.end());
    }

    job->stats.width = job->width;
    job->stats.height = job->height;
    job->stats.texels = (int)job->texels.size();
    job->stats.bakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    job->done = true;
}

void StartLightmapBake(World &world)
{
    CancelLightmapBake(world);
    if (world.lightmapTexels.empty()) return;

    auto job = std::make_shared<LightmapJob>();
    job->triangles = world.bounceTriangles;
    job->propTriangleStart = world.propTriangleStart;
    job->texels = world.lightmapTexels;
    job->traceWorld.colliders = world.colliders;
    job->traceWorld.bounds = world.bounds;
    job->sun = world.lighting.sun;
    job->width = world.lightmapWidth;
    job->height = world.lightmapHeight;
    job->key = LightmapKey(world);
    job->stats = {};
    job->thread = std::thread(RunBake, job.get());
    world.lightmapJob = job;
}

// Puts a finished bake's texels in the lightmap texture
static void ApplyBake(World &world, BounceStats *stats)
{
    LightmapJob &job = *world.lightmapJob;
    if (world.lightmap.id != 0 && world.lightmap.width == job.width && world.lightmap.height == job.height*LIGHTMAP_LAYERS)
    {
        UpdateTexture(world.lightmap, job.pixels.data());
    }
    else
    {
        if (world.lightmap.id != 0) UnloadTexture(world.lightmap);
        Image image = { job.pixels.data(), job.width, job.height*LIGHTMAP_LAYERS, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        world.lightmap = LoadTextureFromImage(image);
        SetTextureFilter(world.lightmap, world.lightmapFiltered ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
        SetTextureWrap(world.lightmap, TEXTURE_WRAP_CLAMP);
    }
    world.lighting.lightmapTexture = world.lightmap.id;
    world.bounceBaked = true;
    world.lightmapKey = job.key;
    if (stats) *stats = job.stats;
    world.lightmapJob.reset();
}

void SetLightmapSettings(World &world, float density, bool filtered)
{
    if (filtered != world.lightmapFiltered)
    {
        world.lightmapFiltered = filtered;
        if (world.lightmap.id != 0) SetTextureFilter(world.lightmap, filtered ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }

    float current = (world.lightmapDensity > 0.0f) ? world.lightmapDensity : LIGHTMAP_DENSITY;
    if (density <= 0.0f) density = LIGHTMAP_DENSITY;
    if (density == current) return;
    world.lightmapDensity = density;
    if (world.model.meshCount == 0) return;  // Laid out when the world loads

    // A new layout makes the lightmap there is now useless, and gives the world a new key, so it gets baked again
    CancelLightmapBake(world);
    BuildLightmapLayout(world);
    world.bounceBaked = false;
    world.lighting.lightmapTexture = 0;
}

bool UpdateLightmapBake(World &world, BounceStats *stats)
{
    if (!world.lightmapJob || !world.lightmapJob->done) return false;
    ApplyBake(world, stats);
    return true;
}

bool FinishLightmapBake(World &world, BounceStats *stats)
{
    if (!world.lightmapJob) return false;
    if (world.lightmapJob->thread.joinable()) world.lightmapJob->thread.join();
    if (!world.lightmapJob->done) return false;
    ApplyBake(world, stats);
    return true;
}

void CancelLightmapBake(World &world)
{
    world.lightmapJob.reset();  // Its destructor stops the thread and waits for it
}

bool IsLightmapBaking(const World &world)
{
    return world.lightmapJob != nullptr;
}

unsigned long long LightmapBakingKey(const World &world)
{
    return world.lightmapJob ? world.lightmapJob->key : 0;
}

// ---------------------------------------------------------------------------------------------
// Saving and loading

std::string LightmapPath(const std::string &levelPath)
{
    std::string base = levelPath;
    if (base.size() > 5 && base.compare(base.size() - 5, 5, ".json") == 0) base.resize(base.size() - 5);
    return base + "_lightmap.png";
}

bool SaveLightmap(const World &world, const char *path)
{
    if (!world.bounceBaked || world.lightmap.id == 0) return false;
    Image image = LoadImageFromTexture(world.lightmap);
    bool saved = ExportImage(image, path);
    UnloadImage(image);
    return saved;
}

bool LoadLightmap(World &world, const char *path, const std::string &keyText)
{
    if (!GameDataFileExists(path)) return false;

    // Checked before reading the file: a lightmap baked for another world model, textures or sun is no use
    unsigned long long key = LightmapKey(world);
    if (keyText != LightmapKeyToText(key))
    {
        TraceLog(LOG_INFO, "LIGHTMAP: [%s] Is out of date: the world model, its textures or the sun changed since it was baked", path);
        return false;
    }

    Image image = LoadImage(path);  // Through the game data, so it can come from gamedata.zip
    if (image.data == nullptr) return false;
    if (image.width != world.lightmapWidth || image.height != world.lightmapHeight*LIGHTMAP_LAYERS)
    {
        TraceLog(LOG_WARNING, "LIGHTMAP: [%s] Is %dx%d, but the world's lightmap layout is %dx%d", path, image.width, image.height, world.lightmapWidth, world.lightmapHeight*LIGHTMAP_LAYERS);
        UnloadImage(image);
        return false;
    }

    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    CancelLightmapBake(world);
    if (world.lightmap.id != 0) UnloadTexture(world.lightmap);
    world.lightmap = LoadTextureFromImage(image);
    SetTextureFilter(world.lightmap, world.lightmapFiltered ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    SetTextureWrap(world.lightmap, TEXTURE_WRAP_CLAMP);
    UnloadImage(image);
    world.lighting.lightmapTexture = world.lightmap.id;
    world.bounceBaked = true;
    world.lightmapKey = key;
    return true;
}
