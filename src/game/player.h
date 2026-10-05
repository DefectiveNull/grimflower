#pragma once

#include "raylib.h"
#include "world.h"

struct Player
{
    Vector3 position;   // Origin of the collision hull, like Quake's entity origin
    Vector3 velocity;
    Vector3 spawn;      // Where to respawn after falling out of the level
    float spawnYaw;     // ...and which way to face
    float yaw;          // Radians, 0 looks down -Z
    float pitch;        // Radians, positive looks up
    bool onGround;
    bool jumpReleased;  // Quake requires letting go of jump before jumping again
    bool flying;        // No gravity, moves where you look, still collides (Quake's MOVETYPE_FLY)
    bool ducked;        // Crouching, with a shorter hull
    int waterLevel;     // Like Half-Life's: 0 dry, 1 feet in water, 2 waist (swimming), 3 eyes under
    bool onLadder;
    float ladderCooldown;   // Seconds until a ladder can be grabbed again, after jumping off one
    bool cursorWasHidden;   // Last frame, to notice when the mouse gets captured
    int mouseSettleFrames;  // Mouse look is skipped for these frames after capturing the mouse
};

struct Volumes;

// yaw: radians, 0 looks down -Z
void InitPlayer(Player &player, Vector3 spawn, float yaw);
void UpdatePlayer(Player &player, const World &world, const Volumes &volumes, float dt);
Camera GetPlayerCamera(const Player &player);

// The player's collision hull in world space, shorter while crouching
BoundingBox GetPlayerBox(const Player &player);
