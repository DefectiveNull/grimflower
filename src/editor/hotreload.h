#pragma once

// Reloads files that change outside the editor, so it doesn't need a restart to pick them up:
//   entitylist.txt   written by build.bat every time it builds the game
//   the world model  the level's .obj, when it's exported again from Blender

#include "editor.h"

// Checks the files every so often and reloads the ones that changed. Call once a frame, before drawing
void UpdateHotReload(EditorState &editor);
