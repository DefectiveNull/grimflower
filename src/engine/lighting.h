#pragma once

#include "raylib.h"
#include <vector>

#define MAX_POINT_LIGHTS 16   // Must match MAX_LIGHTS in the shader in lighting.cpp
#define MAX_SHADOW_LIGHTS 4   // Point lights that can cast shadows at once. Must match the shader too

// A directional light, like sunlight: every surface gets light from the same direction. Casts shadows
struct Sun
{
    Vector3 position;   // Where the sun sits above the level. Only used to draw its debug marker
    Vector3 direction;  // Direction the light travels in, normalized (pointing down for a sun overhead)
    Color color;
    float intensity;    // Brightness of a surface facing the sun head on
    float ambient;      // Light every surface gets, so shadows and faces turned away from the sun aren't black
};

// A light that shines in all directions from a point and fades out at its radius.
// The first MAX_SHADOW_LIGHTS cast shadows while Lighting::pointShadows is on
struct PointLight
{
    Vector3 position;
    Color color;
    float intensity;  // Brightness right next to the light
    float radius;     // Distance where the light has faded out completely
};

// Per-pixel lighting from the sun and point lights, with a shadow map for the sun.
// Everything is sent to the GPU every frame, so changing any light shows up right away
struct Lighting
{
    Sun sun;
    std::vector<PointLight> lights;  // Only the first MAX_POINT_LIGHTS are used
    bool pointShadows;               // Point lights cast shadows. InitLighting turns it on, the game sets it from r_use_pointlight_shadows

    Shader shader;           // Assign to the materials of anything that should be lit
    Shader depthShader;      // Renders depth only, for the shadow map
    Material depthMaterial;
    RenderTexture2D shadowMap;
    Matrix sunViewProjection;   // Takes world positions into the shadow map
    BoundingBox bounds;         // Area the sun's shadow map covers

    // Point light shadows: one texture split into tiles, a row of 6 cube faces per shadowed light.
    // Each face stores the distance from the light to the nearest surface, divided by the light's radius
    Shader pointDepthShader;
    Material pointDepthMaterial;
    RenderTexture2D pointShadowAtlas;
    Matrix pointShadowMatrices[MAX_SHADOW_LIGHTS*6];  // View-projection of each cube face, to find a pixel's spot in the atlas
    int pointShadowSlots[MAX_POINT_LIGHTS];           // Atlas row of each light, or -1 if it has no shadow this frame

    int sunDirectionLoc, sunColorLoc, ambientLoc;
    int sunViewProjectionLoc, shadowMapLoc, shadowMapSizeLoc, shadowDepthScaleLoc;
    int lightCountLoc, lightPositionsLoc, lightColorsLoc, lightRadiiLoc;
    int pointShadowAtlasLoc, pointShadowMatricesLoc, pointShadowSlotsLoc, pointShadowTileSizeLoc;
    int pointDepthLightPositionLoc, pointDepthLightRadiusLoc;
    float shadowDepthScale;  // 1/depth range of the shadow camera, for converting the shadow bias to depth

    // Ambient occlusion, applied while SetLightingAO turns it on: to the ambient light, and aoDirect of it to direct light
    unsigned int aoTexture;  // From RenderAmbientOcclusion this frame, 0 for none
    Vector2 aoScreenSize;    // The size it was rendered at, the same as the screen
    float aoDirect;          // How much the AO also darkens the sun and point lights, 0 to 1
    int aoMapLoc, aoEnabledLoc, aoScreenSizeLoc, aoDirectLoc;

    // Baked sun bounce: a lightmap, read through the mesh's second texture coordinates while SetLightingBounce turns it on
    float bounceScale;       // How strong it is, 0 for none
    unsigned int lightmapTexture;  // 0 for none
    int bounceEnabledLoc, bounceScaleLoc, lightmapLoc;
};

// Loads the shaders and shadow map, and places the sun above a corner of the given area
void InitLighting(Lighting &lighting, BoundingBox bounds);
void UnloadLighting(Lighting &lighting);

// Renders the model's depth from the sun into the shadow map, and from each shadowed point light into
// the point shadow atlas. Call each frame before BeginMode3D
void RenderShadowMap(Lighting &lighting, const Model &model);

// Sends the lights to the shader and binds the shadow map. Call before drawing lit models, inside BeginMode3D
void BeginLighting(const Lighting &lighting);
void EndLighting();

// Between BeginLighting and EndLighting: whether what's drawn next gets the ambient occlusion. DrawWorld
// turns it on for the world mesh only, so nothing else drawn with the lighting shader is affected
void SetLightingAO(const Lighting &lighting, bool enabled);

// The same for baked sun bounce: what's drawn next reads the lightmap through its second texture coordinates (only the world
// mesh, which has them). Its strength is bounceScale
void SetLightingBounce(const Lighting &lighting, bool enabled);
