#pragma once

// The editor's look: its font (Roboto), and switching between raygui's light and dark styles

#include "raylib.h"

// Loads fonts/Roboto-Regular.ttf and the light style. Call after InitWindow, from the project folder
void LoadEditorStyle();
void UnloadEditorStyle();

void SetDarkMode(bool dark);
bool IsDarkMode();

// Text in the editor's font, for text that isn't part of a raygui control
void DrawUIText(const char *text, float x, float y, float size, Color color);
float MeasureUIText(const char *text, float size);
float DrawUITextWrapped(const char *text, float x, float y, float width, float size, Color color);  // Returns its height

// Colors that follow the current style
Color UITextColor();        // Normal text
Color UIMutedTextColor();   // Hints and secondary text
Color ViewportBackground(); // Behind the 3D view
