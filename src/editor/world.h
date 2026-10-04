#pragma once

// Copied from src/game/world.h, keeping only loading and drawing: the editor doesn't need collision traces

#include "raylib.h"
#include "lighting.h"
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
    BoundingBox bounds;   // Around all the level geometry
    Lighting lighting;    // Sun, point lights and shadows
};

// scale converts the OBJ's units to world units, for both drawing and collision
bool LoadWorld(World &world, const char *objPath, float scale);
void UnloadWorld(World &world);

// Renders the sun's shadow map. Call each frame before BeginMode3D
void RenderWorldShadows(World &world);

// Draws the level lit by the sun and point lights. With showColliders, also draws the collision brushes
void DrawWorld(const World &world, bool showColliders);

// Builds one convex brush per object ("o"/"g") in an OBJ file. Each object must be convex.
std::vector<Brush> ComputeColliders(const char *objPath, float scale);
