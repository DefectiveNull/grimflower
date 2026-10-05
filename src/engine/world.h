#pragma once

#include "raylib.h"
#include "lighting.h"
#include "skybox.h"
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

struct World
{
    Model model;
    std::vector<Brush> colliders;
    std::vector<Brush> movers;  // Solids that move, like doors. Whoever moves them rebuilds this every frame
    BoundingBox bounds;   // Around all the level geometry
    Lighting lighting;    // Sun, point lights and shadows
    Skybox skybox;        // From the level's info.skybox, if it has one
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
