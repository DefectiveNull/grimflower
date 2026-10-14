#pragma once

// Volumes and doors, placed as entities with a box (mins and maxs around their origin), like
// Half-Life's brush entities. What a volume does comes from its entity type: func_water is water,
// func_ladder a ladder, and trigger_changelevel a trigger

#include "raylib.h"
#include "world.h"

#include <string>
#include <vector>

// What's at a point. Flags, since volumes can overlap
enum Contents
{
    CONTENTS_EMPTY = 0,
    CONTENTS_WATER = 1,    // func_water: swim
    CONTENTS_LADDER = 2,   // func_ladder: climb
    CONTENTS_TRIGGER = 4,  // trigger_changelevel: does something when the player walks in
};

struct Volume
{
    BoundingBox box;   // In world space
    int contents;
    std::string map;   // For trigger_changelevel: the level it loads, as a game data path
    Model surface;     // For func_water: its faces, textured with WATER_TEXTURE. meshCount 0 for the others
};

enum DoorState
{
    DOOR_CLOSED,
    DOOR_OPENING,
    DOOR_OPEN,
    DOOR_CLOSING
};

// A solid textured box that slides open when the player walks into it, then closes again, like func_door
struct Door
{
    BoundingBox closedBox;  // In world space
    Vector3 openOffset;     // How far it moves to open
    float speed;            // World units per second
    float wait;             // Seconds it stays open, or -1 to stay open
    float amount;           // 0 closed to 1 open
    float waitTimer;
    DoorState state;
    Model model;            // A box the door's size, with its texture
    Texture texture;
    WorldMaterial material; // Its texture's material from .materials: bump, specular and gloss maps, and reflection
};

// A looping sound heard inside a box, like wind in a canyon or the hum of a machine room
// (trigger_soundscape). It fades in over fadeDistance from the box's sides, so walking out fades it
// smoothly to nothing, and it's panned towards the entity's origin, so it sounds like it comes from there
struct Soundscape
{
    BoundingBox box;
    Vector3 origin;
    float volume;          // At full strength, 0 to 1
    float fadeDistance;    // World units in from the sides where it's still fading in
    int sound;             // Ambient sound handle, -1 if it couldn't be loaded
    float heardVolume;     // How loud it is now, after the fade
    float heardPan;        // -1 left to 1 right
};

struct Volumes
{
    std::vector<Volume> volumes;
    std::vector<Door> doors;
    std::vector<Soundscape> soundscapes;
    bool levelChangeStarted;  // A trigger_changelevel fired, so the others don't too

    // Shared by every water surface, loaded with the first one
    Shader waterShader;
    Texture waterTexture;
    int waterTimeLoc;
    bool waterLoaded;
};

// Every kind of volume a point is inside, or a box overlaps
int PointContents(const Volumes &volumes, Vector3 point);
int BoxContents(const Volumes &volumes, BoundingBox box);

// Gives a water volume its surface: every face of its box, with the water texture warping like Quake's
void AddWaterSurface(Volumes &volumes, Volume &volume);

// direction: which way it opens. It moves its own size that way, minus lip. shader: what the door is drawn with
void AddDoor(Volumes &volumes, BoundingBox closedBox, Vector3 direction, float lip, float speed, float wait,
             const char *texturePath, Shader shader);
void UnloadVolumes(Volumes &volumes);

// Starts a soundscape's sound, silent until UpdateSoundscapes finds the player inside
void AddSoundscape(Volumes &volumes, BoundingBox box, Vector3 origin, const char *soundPath, float volume, float fadeDistance);

// Sets each soundscape's volume from how far inside it the listener is, and its pan from which side its
// origin is on. listener: the camera's position. yaw: which way it faces, like the player's. Call once a frame
void UpdateSoundscapes(Volumes &volumes, Vector3 listener, float yaw);

// Opens doors the player walks into and moves them, putting them in world.movers for collision, and fires
// the trigger_changelevel the player is in. Call before moving the player
void UpdateVolumes(Volumes &volumes, World &world, BoundingBox playerBox, float dt);

// Doors, lit, and water surfaces. With showAll, outlines of every volume too. Call inside BeginMode3D
void DrawVolumes(const Volumes &volumes, const World &world, bool showAll);
