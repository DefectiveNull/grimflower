#pragma once

#include <vector>

struct Resolution
{
    int width;
    int height;
};

struct Settings
{
    // Video
    int resolution;          // Index into resolutions
    bool fullscreen;

    // Audio
    float masterVolume;      // 0 to 1

    // Game
    float mouseSensitivity;  // Quake style, 3 = 0.003 radians per pixel
    bool invertMouseY;
};

extern Settings settings;
extern std::vector<Resolution> resolutions;  // Only the ones that fit the monitor

// Fills the resolution list and picks the one matching the current window. Call after InitWindow
void InitSettings();

void ApplyVideoSettings();
void ApplyAudioSettings();
