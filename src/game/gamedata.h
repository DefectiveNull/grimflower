#pragma once

// Mounts a zip so every raylib file load (models, materials, textures) reads from it, using paths
// relative to the zip's root, like "model/dev.obj". Files that aren't in the zip still load from disk.
// Call after changing into the game's folder
bool MountGameData(const char *zipPath);
void UnmountGameData();

// True if the file is in the project folder, the mounted zip or on disk
bool GameDataFileExists(const char *fileName);

// When the game runs from inside the project (build/game.exe), uses the project's own maps/, model/,
// textures/ and cfg/ before gamedata.zip, so saving in the editor or re-exporting a model shows up without
// rebuilding. A shipped game has no project folder around it, so it just uses the zip. Returns true if found
bool UseProjectFolder();
