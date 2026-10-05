#pragma once

// The game's own console commands, added to the engine's (src/engine/cvars.cpp) at startup:
// the movement buttons, fly and map

// What the player is being told to do, set by the +/- button commands (+forward, -forward...)
struct InputButtons
{
    bool forward;
    bool back;
    bool moveLeft;
    bool moveRight;
    bool jump;       // Also flies up
    bool moveUp;
    bool moveDown;
    bool duck;       // Crouch, or swim and fly down
    bool toggleFly;  // Set by the "fly" command, cleared once the player handles it
};

extern InputButtons inputButtons;

// Adds the game's commands to the console. Call before running any configs, which bind keys to them
void AddGameCommands();
