#pragma once

#include <string>

// Native Windows open/save dialogs. filter is pairs of description and pattern, each ending in \0,
// like "Level files (*.json)\0*.json\0". owner is the window to show them over (raylib's GetWindowHandle())
bool ShowOpenFileDialog(void *owner, const char *title, const char *filter, const std::string &startFolder, std::string &path);
bool ShowSaveFileDialog(void *owner, const char *title, const char *filter, const char *defaultExtension,
                        const std::string &startFolder, std::string &path);
