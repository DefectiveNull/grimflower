#pragma once

// The video and game options, kept as cvars so they're saved to config.cfg with the binds and loaded back
// at startup.
// The options menu changes them and then calls SaveSettings; the console can too

#include "cvars.h"

#include <vector>

struct Resolution
{
    int width;
    int height;
};

// Video
extern Cvar vid_width;
extern Cvar vid_height;
extern Cvar vid_fullscreen;  // 0/1

// Audio: the volume cvars are the engine's, in audio.h

// Game
extern Cvar sensitivity;     // Mouse look, Quake style: 3 = 0.003 radians per pixel
extern Cvar m_invert;        // Inverted vertical aim, 0/1

extern std::vector<Resolution> resolutions;  // Common sizes that fit the monitor, plus the saved one if it's not among them

// Fills the resolution list and adds the settings cvars to the console. Call after InitWindow and
// before running the configs, which set the cvars
void InitSettings();

// Applies the video cvars to the window. Call after the configs, and after changing one.
// The audio ones apply themselves every frame, in UpdateAudio
void ApplyVideoSettings();

// Index in resolutions of the vid_width x vid_height one
int CurrentResolution();

// Writes the settings and binds to config.cfg
void SaveSettings();
