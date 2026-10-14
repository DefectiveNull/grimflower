#pragma once

// Sound, on two channels with their own volumes on top of the master volume:
//   world  one-shot sounds like footsteps and doors, loaded once and kept for the next time
//   music  one streamed track at a time
// Everything is read through the game data, so it can come from gamedata.zip

#include "cvars.h"

extern Cvar volume;        // Master volume, 0 to 1
extern Cvar world_volume;  // World channel, 0 to 1, times the master volume
extern Cvar music_volume;  // Music channel, 0 to 1, times the master volume

// Opens the audio device and adds the volume cvars and the play and music commands to the console.
// Call after InitWindow and before running the configs, which set the volumes
void InitAudio();
void CloseAudio();

// Keeps the music streaming and applies volume changes. Call once a frame
void UpdateAudio();

// Plays a sound on the world channel, like "sound/door.wav". Returns false if it couldn't be loaded
bool PlayWorldSound(const char *path);

// Ambient sounds: loops on the world channel that keep playing, like a soundscape's wind or hum. They
// start silent; whoever starts one sets its volume and pan as the player moves, and stops it when done.
// Returns a handle, or -1 if it couldn't be loaded
int StartAmbientSound(const char *path);
void SetAmbientSound(int handle, float volume, float pan);  // volume: 0 to 1, times the world channel's. pan: -1 left to 1 right
void StopAmbientSound(int handle);

// Starts a track on the music channel in place of the current one. Returns false if it couldn't be loaded
bool PlayMusic(const char *path, bool loop);
void StopMusic();
