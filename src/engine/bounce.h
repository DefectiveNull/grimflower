#pragma once

// Sun bounce light: a simplified, one bounce radiosity bake, from the sun only, stored in a lightmap.
//
// Layout: when the world loads, its triangles are grouped into flat charts (connected triangles in one
// plane), each given a rectangle in one lightmap texture, one texel (luxel) per LIGHTMAP_LUXEL_SIZE world units. The
// world mesh gets lightmap texture coordinates to match. It's worked out from the level's geometry alone, so
// the game and the editor always agree on it, and a saved lightmap fits.
//
// Bake: every lightmap texel on the world traces 26 directions (the 6 axes, the 12 edge and the 8 corner
// diagonals of a cube around it). Where a trace hits the world, and the sun reaches that spot, the surface
// there is lit and bounces some of that light back, tinted by its texture. Each texel adds up what reaches
// it. Bakes run on a background thread; the sun's direct light and shadows stay fully dynamic.

#include "world.h"

#include <string>

// The lightmap texture has the layout's height four times over: the bounce reaching each texel head on, then
// from each of three tilted directions, for bump mapped materials (Half-Life 2's radiosity normal mapping).
// The lighting shader reads them the same way
static const int LIGHTMAP_LAYERS = 4;

static const float LIGHTMAP_LUXEL_SIZE = 16.0f;  // Default world units per lightmap texel, like Quake's lightmaps

struct BounceStats
{
    int width, height;     // The lightmap's size
    int texels;            // Texels that lie on the world, and get baked
    long long rays;        // Traces: 52 per texel (the ones in front of it), plus one towards the sun per hit
    double bakeMs;         // How long the bake took, on its thread
};

// Groups the world into charts, lays them out in the lightmap, and gives the world mesh lightmap texture
// coordinates. SetWorldGeometry calls this, when it builds everything
void BuildLightmapLayout(World &world);

// The level's lightmap settings. luxelSize: world units per lightmap texel, 0 for LIGHTMAP_LUXEL_SIZE. Changing it
// lays the lightmap out again, so it needs a new bake. filtered: smooth (bilinear) or blocky (nearest) like
// software Quake, which takes effect right away. Call before SetWorldGeometry, or any time after
void SetLightmapSettings(World &world, float luxelSize, bool filtered);

// Starts baking the bounce of world.lighting.sun on a background thread, cancelling any bake still running
void StartLightmapBake(World &world);

// Call once a frame: when a bake has finished, puts it in the lightmap texture and returns true, with its stats
bool UpdateLightmapBake(World &world, BounceStats *stats);

// Waits for a running bake to finish and applies it, like before saving. Returns false if there wasn't one
bool FinishLightmapBake(World &world, BounceStats *stats);

// Stops a running bake without applying it, like when the world is unloaded
void CancelLightmapBake(World &world);
bool IsLightmapBaking(const World &world);

// A fingerprint of everything a bake depends on: the world's triangles, their colors, the lightmap layout,
// and the sun (where it is, its direction, color and intensity). A lightmap is up to date when the key it
// was baked with (world.lightmapKey) matches the world's key now
unsigned long long LightmapKey(const World &world);
unsigned long long LightmapBakingKey(const World &world);  // The key of the bake running now, 0 for none

// Keys as text, for the level file's info.lightmapKey
std::string LightmapKeyToText(unsigned long long key);

// The lightmap file for a level: maps/dev.json has maps/dev_lightmap.png
std::string LightmapPath(const std::string &levelPath);

// Saves the baked lightmap as a PNG. Loading reads it through the game data, and only uses it if keyText,
// the key saved with it, matches the world's key now: if the geometry, its textures or the sun have
// changed since, it returns false, and the world needs a fresh bake
bool SaveLightmap(const World &world, const char *path);
bool LoadLightmap(World &world, const char *path, const std::string &keyText);
