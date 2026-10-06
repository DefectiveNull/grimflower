#pragma once

#include "raylib.h"
#include "lighting.h"
#include "skybox.h"
#include "ssao.h"
#include <memory>
#include <string>
#include <vector>

struct Plane
{
    Vector3 normal;  // Points out of the brush
    float dist;      // dot(normal, point) == dist for points on the plane
};

// A convex solid bounded by planes, like a Quake map brush
struct Brush
{
    std::vector<Plane> planes;                 // Face planes plus bevel planes
    std::vector<std::vector<Vector3>> faces;   // Face outlines, for debug drawing
};

// A triangle of the world model as it was loaded, in world space, for tracing bounce light against
struct BounceTriangle
{
    Vector3 a, b, c;
    Vector3 normal;
    Vector3 albedo;  // Its texture's average color, 0 to 1: how much light it bounces
};

// A lightmap texel that lies on the world: where it is, which way it faces, and where it is in the lightmap
struct LightmapTexel
{
    Vector3 position;
    Vector3 normal;
    int x, y;
};

struct LightmapJob;  // A background bake, see bounce.cpp

struct World
{
    Model model;
    std::vector<Brush> colliders;
    std::vector<Brush> movers;  // Solids that move, like doors. Whoever moves them rebuilds this every frame
    BoundingBox bounds;   // Around all the level geometry
    Lighting lighting;    // Sun, point lights and shadows
    Skybox skybox;        // From the level's info.skybox, if it has one
    AmbientOcclusion ao;  // Screen space ambient occlusion of the world mesh, see RenderWorldAO

    // Sun bounce light, in a lightmap texture (bounce.cpp). The lightmap's layout comes from the world
    // model alone, worked out when it loads, so a lightmap saved by the editor fits it in the game
    bool bounceBaked;                                // lightmap holds a bake
    Texture2D lightmap;
    int lightmapWidth, lightmapHeight;
    std::vector<BounceTriangle> bounceTriangles;     // What bounce traces hit
    std::vector<LightmapTexel> lightmapTexels;       // Where each lightmap texel is in the world
    std::shared_ptr<LightmapJob> lightmapJob;        // A bake running in the background, if any
    unsigned long long lightmapGeometryKey;          // Fingerprint of the triangles, colors and layout, see LightmapKey
    unsigned long long lightmapKey;                  // The key lightmap was baked with, 0 for none
};

// Result of sweeping a box through the world, like Quake's trace_t
struct Trace
{
    float fraction;   // How far along start->end the box got before hitting something (1 = no hit)
    Vector3 endPos;   // Where the box stopped
    Vector3 normal;   // Normal of the surface that was hit (zero if nothing was hit)
    bool startSolid;  // The box started inside a collider
};

// scale converts the OBJ's units to world units, for both drawing and collision
bool LoadWorld(World &world, const char *objPath, float scale);
void UnloadWorld(World &world);

// Renders the sun's shadow map. Call each frame before BeginMode3D
void RenderWorldShadows(World &world);

// Works out the world mesh's ambient occlusion for this view, which DrawWorld then applies to the world
// mesh only. Call each frame before BeginMode3D, with the screen size. enabled false turns it off.
// radius: in world units, how far around each point to look for things hemming it in
void RenderWorldAO(World &world, Camera camera, int width, int height, bool enabled, float radius);

// Draws the level lit by the sun and point lights. With showColliders, also draws the collision brushes
void DrawWorld(const World &world, bool showColliders);

// A box-shaped brush, for movers
Brush MakeBoxBrush(BoundingBox box);

// Loads a texture through the game data, so it can come from gamedata.zip. TGAs work too, which the
// prebuilt raylib can't read. Returns false if it couldn't be loaded
bool LoadGameTexture(const char *path, Texture &texture);

// Builds one convex brush per object ("o"/"g") in an OBJ file. Each object must be convex.
std::vector<Brush> ComputeColliders(const char *objPath, float scale);

// Sweeps a box with the given extents (relative to its origin) from start to end, against the colliders and the movers
Trace TraceBox(const World &world, Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs);
