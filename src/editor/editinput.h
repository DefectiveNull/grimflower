#pragma once

// The mouse and keyboard, as Edit mode and the 3D view read them. Normally raylib's input; a test program can
// script it instead (EditInputScript), frame by frame, to drive the tools without anyone at the keyboard

#include "raylib.h"

#include <set>

struct EditInputScript
{
    bool active = false;
    Vector2 mouse = { 0.0f, 0.0f };
    bool leftDown = false, leftPressed = false, rightDown = false;
    float wheel = 0.0f;
    std::set<int> keysDown, keysPressed;
};
extern EditInputScript editInputScript;

inline Vector2 EdMousePosition() { return editInputScript.active ? editInputScript.mouse : GetMousePosition(); }
inline bool EdLeftPressed() { return editInputScript.active ? editInputScript.leftPressed : IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
inline bool EdLeftDown() { return editInputScript.active ? editInputScript.leftDown : IsMouseButtonDown(MOUSE_BUTTON_LEFT); }
inline float EdWheel() { return editInputScript.active ? editInputScript.wheel : GetMouseWheelMove(); }
inline bool EdKeyPressed(int key) { return editInputScript.active ? editInputScript.keysPressed.count(key) > 0 : IsKeyPressed(key); }
inline bool EdKeyDown(int key) { return editInputScript.active ? editInputScript.keysDown.count(key) > 0 : IsKeyDown(key); }
