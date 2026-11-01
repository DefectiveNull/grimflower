#pragma once

#include <string>

// Every model format the game can load, for a dialog's filter. See src/engine/modelfile.h
#define MODEL_FILE_FILTER "Models (*.obj;*.fbx;*.gltf;*.glb;*.dae;*.3ds;*.ply;*.stl)\0*.obj;*.fbx;*.gltf;*.glb;*.dae;*.3ds;*.ply;*.stl\0All files (*.*)\0*.*\0"

// Native Windows open/save dialogs. filter is pairs of description and pattern, each ending in \0,
// like "Level files (*.json)\0*.json\0". owner is the window to show them over (raylib's GetWindowHandle())
bool ShowOpenFileDialog(void *owner, const char *title, const char *filter, const std::string &startFolder, std::string &path);
bool ShowSaveFileDialog(void *owner, const char *title, const char *filter, const char *defaultExtension,
                        const std::string &startFolder, std::string &path);
