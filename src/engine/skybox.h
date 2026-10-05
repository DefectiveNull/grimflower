#pragma once

// A sky drawn from an equirectangular (360 by 180 degree) image, like the .hdr files from Poly Haven.
// It's drawn first, on a cube around the camera that's always behind everything else

#include "raylib.h"
#include <string>

struct Skybox
{
    bool loaded = false;
    std::string path;   // Game data path it was loaded from
    Texture2D texture;  // Kept as floating point, so the HDR's bright sun survives until tonemapping
    Shader shader;
    Model cube;
    int exposureLoc;
    float exposure;     // Brightness multiplier before tonemapping
};

// Loads an .hdr (or .png/.jpg/.tga) sky through the game data. Returns false if it couldn't
bool LoadSkybox(Skybox &skybox, const char *path);
void UnloadSkybox(Skybox &skybox);

// Draws the sky around the camera. Call first inside BeginMode3D, before the world
void DrawSkybox(const Skybox &skybox, Camera camera);
