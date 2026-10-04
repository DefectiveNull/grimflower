#pragma once

// Mounts a zip so every raylib file load (models, materials, textures) reads from it, using paths
// relative to the zip's root, like "model/dev.obj". Files that aren't in the zip still load from disk.
// Call after changing into the game's folder
bool MountGameData(const char *zipPath);
void UnmountGameData();

// True if the file is in the mounted zip or on disk
bool GameDataFileExists(const char *fileName);
