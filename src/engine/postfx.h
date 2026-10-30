#pragma once

// Post processing: the 3D view is drawn into a render texture, then onto the screen through one shader that
// grades it. In order: the level's color grade (a gradient map, from info_colorcorrection), then the player's
// contrast, saturation and gamma, then film grain. The HUD and menus are drawn after it, so they're left alone

#include "raylib.h"

// A level's color palette, as a gradient map: each pixel's brightness picks a color along the gradient, from
// its left end for black to its right end for white
struct ColorGrade
{
    Texture2D gradient;  // id 0 for none: the level isn't graded
    float strength;      // How much of the gradient map's color replaces the original, 0 to 1
};

struct PostProcess
{
    bool ready;
    RenderTexture2D target;  // The 3D view, at the screen's size
    Shader shader;
    int gradientLoc, gradientStrengthLoc, contrastLoc, saturationLoc, gammaLoc, filmGrainLoc, timeLoc;
};

// The player's settings, from the r_contrast, r_saturation, r_filmgrain and r_gamma cvars
struct PostSettings
{
    float contrast;    // 1 unchanged, more spreads darks and lights apart
    float saturation;  // 1 unchanged, 0 grayscale
    float filmGrain;   // 0 none, 1 strong
    float gamma;       // 1 unchanged, more brightens the darks and midtones
};

// Between BeginDrawing and EndDrawing, after any passes of their own (shadows, ambient occlusion): what's
// drawn until EndPostProcess goes into the render texture, which is made or resized to width x height
void BeginPostProcess(PostProcess &post, int width, int height);

// Draws the render texture onto the screen, graded
void EndPostProcess(const PostProcess &post, const ColorGrade &grade, const PostSettings &settings);

void UnloadPostProcess(PostProcess &post);

// Loads a level's color grade. gradientPath is an image through the game data, read along its middle row; empty
// (or one that can't be loaded) makes a two color gradient from shadows to highlights instead. Returns false
// if gradientPath was given but couldn't be loaded
bool LoadColorGrade(ColorGrade &grade, const char *gradientPath, Color shadows, Color highlights, float strength);
void UnloadColorGrade(ColorGrade &grade);
