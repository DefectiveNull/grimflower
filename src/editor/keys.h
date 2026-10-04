#pragma once
// Copied from src/game/keys.h

// Key names for binds, like Quake's: "w", "space", "ctrl", "f1", "mouse1", "mwheelup"...
// Keyboard keys use raylib's key codes; mouse buttons and the wheel get codes above those

#include <string>

static const int KEY_CODE_MOUSE = 1000;            // + raylib's MouseButton
static const int KEY_CODE_MOUSE_WHEEL_UP = 1100;
static const int KEY_CODE_MOUSE_WHEEL_DOWN = 1101;

// -1 if there's no key with that name. Not case sensitive
int KeyFromName(const std::string &name);
std::string KeyToName(int key);

// "shift", "ctrl" and "alt" count either the left or right key. The mouse wheel is pressed and
// released in the same frame it moves
bool IsInputPressed(int key);
bool IsInputReleased(int key);
