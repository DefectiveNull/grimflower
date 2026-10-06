#pragma once

// Screen space ambient occlusion: darkens the ambient light in creases and corners, worked out each
// frame from the depth and normals of what's on screen. Only the world mesh goes into it and only
// the world mesh gets it, see RenderWorldAO and DrawWorld in world.cpp

#include "raylib.h"

struct AmbientOcclusion
{
    bool ready;              // Loaded, at width x height
    int width, height;

    RenderTexture2D gbuffer;  // Per pixel: view space normal (xyz) and distance from the camera (w), 0 where there's nothing
    RenderTexture2D raw;      // Occlusion, noisy: 1 = open, 0 = fully hemmed in
    RenderTexture2D blurred;  // The same, smoothed. What the lighting shader reads

    Shader gbufferShader;
    Material gbufferMaterial;
    Shader ssaoShader;
    Shader blurShader;
    Texture2D noise;          // 4x4 random rotations, so the samples differ between neighbouring pixels

    int ssaoGbufferLoc, ssaoNoiseLoc, ssaoProjectionLoc, ssaoProjScaleLoc, ssaoScreenSizeLoc, ssaoRadiusLoc;
    int blurSourceLoc, blurScreenSizeLoc;
};

// Draws the model's normals and depth from the camera, then the occlusion into ao.blurred. radius: in world
// units, how far around each point to look for things hemming it in. Loads, or
// reloads at a new size, as needed. Call outside BeginMode3D
void RenderAmbientOcclusion(AmbientOcclusion &ao, const Model &model, Camera camera, int width, int height, float radius);
void UnloadAmbientOcclusion(AmbientOcclusion &ao);
