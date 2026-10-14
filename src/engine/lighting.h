#pragma once

#include "raylib.h"
#include <vector>

#define MAX_POINT_LIGHTS 32   // Point lights that light the world at once. The shader in lighting.cpp gets it as MAX_LIGHTS
#define MAX_SHADOW_LIGHTS 32  // Point lights that can cast shadows at once. The shader gets it too
#define MAX_CUBEMAPS 4        // Cubemaps the shader can reflect at once: the ones nearest the camera

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

    // Point light shadows: one texture split into tiles, a row of 6 cube faces per shadowed light. It has
    // rows for as many shadowed lights as the level has had so far (4, 8, 16 or 32), so levels with few
    // lights don't pay for 32 rows of video memory.
    // Each face stores the distance from the light to the nearest surface, divided by the light's radius
    Shader pointDepthShader;
    Material pointDepthMaterial;
    RenderTexture2D pointShadowAtlas;
    int pointShadowRows;                     // Rows the atlas has room for
    int pointShadowSlots[MAX_POINT_LIGHTS];  // Atlas row of each light, or -1 if it has no shadow this frame

    int sunDirectionLoc, sunColorLoc, ambientLoc;
    int sunViewProjectionLoc, shadowMapLoc, shadowMapSizeLoc, shadowDepthScaleLoc;
    int lightCountLoc, lightPositionsLoc, lightColorsLoc, lightRadiiLoc;
    int pointShadowAtlasLoc, pointShadowSlotsLoc, pointShadowTileSizeLoc, pointShadowRowsLoc;
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

    // The material of the mesh drawn next, see SetLightingMaterial
    int bumpMapLoc, specularMapLoc, glossMapLoc, hasBumpMapLoc, bumpGreenUpLoc, hasSpecularMapLoc, hasGlossMapLoc;
    int specularStrengthLoc, glossinessLoc, viewPositionLoc, reflectionLoc;
    bool unlit;              // Textures only, no lighting: the editor's Shaded Unlit and Wireframe views
    int unlitLoc;

    // Cubemaps (env_cubemap) for reflections: every one in the level. BeginLighting picks the MAX_CUBEMAPS
    // nearest the camera. capturingCubemaps turns reflections off while they're being captured
    std::vector<Vector3> cubemapPositions;
    std::vector<unsigned int> cubemapTextures;
    bool capturingCubemaps;
    int cubemapLocs[MAX_CUBEMAPS], cubemapPositionsLoc, cubemapCountLoc;
};

// Loads the shaders and shadow map, and places the sun above a corner of the given area
void InitLighting(Lighting &lighting, BoundingBox bounds);
void UnloadLighting(Lighting &lighting);

// Something drawn into the shadow maps: a model, placed by transform (the world mesh, or a prop)
struct ShadowCaster
{
    const Model *model;
    Matrix transform;
};

// Renders the casters' depth from the sun into the shadow map, and from each shadowed point light into
// the point shadow atlas. Call each frame before BeginMode3D
void RenderShadowMap(Lighting &lighting, const std::vector<ShadowCaster> &casters);

// Sends the lights to the shader and binds the shadow map. Call before drawing lit models, inside BeginMode3D
void BeginLighting(const Lighting &lighting);
void EndLighting();

// Between BeginLighting and EndLighting: whether what's drawn next gets the ambient occlusion. DrawWorld
// turns it on for the world mesh only, so nothing else drawn with the lighting shader is affected
void SetLightingAO(const Lighting &lighting, bool enabled);

// The same for baked sun bounce: what's drawn next reads the lightmap through its second texture coordinates (only the world
// mesh, which has them). Its strength is bounceScale
void SetLightingBounce(const Lighting &lighting, bool enabled);

// Between BeginLighting and EndLighting: the bump, specular and gloss maps (texture ids, 0 for none) and
// settings of what's drawn next (materials.h). BeginLighting starts with none: no bump and no shine
void SetLightingMaterial(const Lighting &lighting, unsigned int bumpMap, unsigned int specularMap, unsigned int glossMap,
                         bool bumpGreenUp, float specular, float gloss, float reflection);
void ClearLightingMaterial(const Lighting &lighting);
