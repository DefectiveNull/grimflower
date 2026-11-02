#pragma once

#include "raylib.h"
#include "geometry.h"
#include "lighting.h"
#include "materials.h"
#include "skybox.h"
#include "postfx.h"
#include "ssao.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

// How near and far the camera sees, in world units (Quake units). Set with rlSetClipPlanes when the window opens
static const double VIEW_NEAR = 1.0;
static const double VIEW_FAR = 16384.0;

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
    Vector3 tangent, bitangent;  // Which ways its texture's x and y run across it, for bump mapped bounce
    int x, y;
};

struct LightmapJob;  // A background bake, see bounce.cpp

// A material's maps besides its diffuse texture, from .materials (materials.h)
struct WorldMaterial
{
    std::string diffusePath;   // Its diffuse texture, as a game data path. The material is looked up by this
    MaterialDef def;
    Texture bumpmap, specularmap, glossmap;  // id 0 where it has none
};

// What the level looks like from one spot, in all six directions, for reflections (env_cubemap)
struct WorldCubemap
{
    Vector3 position;
    int size;              // Pixels along each face's side
    unsigned int texture;  // The cubemap
};

// A model props are made from, loaded once however many props use it
struct PropModel
{
    std::string path;                       // The model file, as a game data path
    Model model;                            // meshCount 0 if it couldn't be loaded
    std::vector<WorldMaterial> materials;
    BoundingBox bounds;                     // In the model's own units
};

// A model placed in the level, like a crate or a lamp (prop_model)
struct WorldProp
{
    int model;           // Index into World::propModels
    Matrix transform;    // Its scale, turn and place
    BoundingBox bounds;  // Around it in world space
    bool isStatic;       // Part of the bounce bake
    std::vector<Mesh> lightmapMeshes;  // A static prop's own copies of its model's meshes, with lightmap coordinates. Empty until BuildLightmapLayout
    bool detailLighting = false;       // Not static: gets the bounce from the ground under it all over, like Half-Life 2's detail props
    bool hasBounceSpot = false;        // ...found there, at this spot in the lightmap. See FindPropBounceSpots
    Vector2 bounceSpot = { 0.0f, 0.0f };
};

struct World
{
    // The level's geometry (geometry.h), drawn as one model with a mesh per material, in world space
    Model model;
    std::vector<Brush> colliders;     // Each mesh's convex pieces, see GeoConvexPieces
    bool hasGeometryBuilt;            // SetWorldGeometry has run with full on: colliders and the lightmap layout match the model
    std::map<std::string, Texture> textureCache;          // Diffuse textures by path, kept while the geometry is rebuilt
    std::map<std::string, WorldMaterial> materialCache;   // And their materials
    std::vector<Brush> movers;  // Solids that move, like doors. Whoever moves them rebuilds this every frame
    std::vector<Brush> propColliders;  // Boxes around solid props. Kept apart from colliders, so bounce bakes don't see them
    std::vector<PropModel> propModels;
    std::vector<WorldProp> props;
    bool propsHidden;    // Not drawn and casting no shadows, like when the editor hides them. Still baked
    std::vector<WorldCubemap> cubemaps;
    bool cubemapsStale;  // Added, or the lighting changed (like a bounce bake finishing): capture them again with CaptureCubemaps
    BoundingBox bounds;   // Around all the level geometry
    Lighting lighting;    // Sun, point lights and shadows
    Skybox skybox;        // From the level's info.skybox, if it has one
    ColorGrade colorGrade;  // From the level's info_colorcorrection, if it has one
    AmbientOcclusion ao;  // Screen space ambient occlusion of the world mesh, see RenderWorldAO
    std::vector<WorldMaterial> materials;  // One per model material, in the same order

    // Sun bounce light, in a lightmap texture (bounce.cpp). The lightmap's layout comes from the world
    // model alone, worked out when it loads, so a lightmap saved by the editor fits it in the game
    bool bounceBaked;                                // lightmap holds a bake
    Texture2D lightmap;
    float lightmapLuxelSize = 0.0f;                  // World units per lightmap texel, 0 for LIGHTMAP_LUXEL_SIZE. See SetLightmapSettings
    bool lightmapFiltered = true;                    // Bilinear, or nearest for blocky luxels
    int lightmapWidth, lightmapHeight;
    std::vector<BounceTriangle> bounceTriangles;     // What bounce traces hit: the world's triangles, then static props'
    int propTriangleStart;                           // Where static props' start in bounceTriangles
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

// Sets up an empty world: its lighting, with a default sun. Then give it geometry with SetWorldGeometry
void CreateWorld(World &world);
void UnloadWorld(World &world);

// Builds the world's model from the level's geometry. full also works out its collision (convex pieces)
// and its bounce lightmap layout, which takes longer: the editor leaves those out while geometry is being
// edited, and the world has no bounce light until a full build. Its bounds come from the geometry. texelsPerUnit is
// the level's texture density, LevelInfo::texelsPerUnit
void SetWorldGeometry(World &world, const Geometry &geometry, float texelsPerUnit, bool full);

// A convex brush from its faces' outlines, with the bevel planes collision needs
Brush MakeConvexBrush(const std::vector<std::vector<Vector3>> &faces);

// A texture's size in pixels, read from its file through the game data without loading it
bool GetGameTextureSize(const char *path, int &width, int &height);

// Renders the sun's shadow map. Call each frame before BeginMode3D
void RenderWorldShadows(World &world);

// Works out the world mesh's ambient occlusion for this view, which DrawWorld then applies to the world
// mesh only. Call each frame before BeginMode3D, with the screen size. enabled false turns it off.
// radius: in world units, how far around each point to look for things hemming it in
void RenderWorldAO(World &world, Camera camera, int width, int height, bool enabled, float radius);

// Loads each world and prop material's bump, specular and gloss maps from the library, replacing the ones
// it had. SetWorldGeometry does this with .materials; the editor again after changing a material
void ApplyMaterials(World &world, const MaterialLibrary &library);

// Props: loads a model for them (once per path, through the game data, with its textures and
// .materials). Returns its index in world.propModels, or -1 if it couldn't be loaded
int LoadPropModel(World &world, const std::string &modelPath);

// Places a prop: scaled from the model's units, turned angle degrees around its own up (like
// info_player_start's angle, with up straight up), tipped so its up points along up, and moved to origin.
// Props are drawn with the world and cast shadows. A solid one blocks movement with the box around it, which
// stays where it was placed. A static one is part of the bounce bake, with its own part of the lightmap. Lay
// the lightmap out again (BuildLightmapLayout) after placing static props. One with detailLighting instead gets the
// bounce from the ground under it: call FindPropBounceSpots after placing those
void AddProp(World &world, int model, Vector3 origin, Vector3 up, float angle, float scale, bool isStatic, bool solid, bool detailLighting = false);
void ClearProps(World &world);  // Removes every prop placed, keeping their models loaded

// A material for something besides the world model and props, like a door: its maps from the library,
// looked up by its diffuse texture. Unload it when done
WorldMaterial LoadWorldMaterial(const std::string &diffusePath, const MaterialLibrary &library);
void UnloadWorldMaterial(WorldMaterial &material);

// Between BeginLighting and EndLighting: what's drawn next uses this material's maps and settings
void SetLightingWorldMaterial(const Lighting &lighting, const WorldMaterial &material);

// Cubemaps for reflections (env_cubemap): size is the pixels along each face. They're black until captured
void AddCubemap(World &world, Vector3 position, int size);
void ClearCubemaps(World &world);

// Renders the level into every cubemap, with its lights where they are now and its lightmap as it is: the
// game does it once when a level loads (and again when a bounce bake finishes), so lights that move later
// don't change what's reflected. Call outside BeginTextureMode
void CaptureCubemaps(World &world);

// Draws the level lit by the sun and point lights. With showColliders, also draws the collision brushes
void DrawWorld(const World &world, bool showColliders);

// A box-shaped brush, for movers
Brush MakeBoxBrush(BoundingBox box);

// Loads a texture through the game data, so it can come from gamedata.zip. TGAs work too, which the
// prebuilt raylib can't read. Returns false if it couldn't be loaded
bool LoadGameTexture(const char *path, Texture &texture);

// Gets an image ready to be a cutout texture, the way Fitzquake and Ironwail do: every fully see-through texel takes
// the average color of the visible texels next to it, so filtering fades a visible edge out in its own color
// instead of whatever the hidden part was painted (often black, giving dark outlines). Turns images that can have
// alpha into RGBA. Returns whether it's a cutout: any texel not fully opaque. Loading textures does this
bool FixAlphaEdges(Image &image);

// Sweeps a box with the given extents (relative to its origin) from start to end, against the colliders and the movers
Trace TraceBox(const World &world, Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs);
