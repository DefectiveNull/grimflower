#pragma once

#include <string>

#define GAME_NAME "Grimflower"

// Asks for a level to be loaded, as a game data path like "maps/dev.json". The main loop loads it at
// the start of the next frame, since the level, world and player belong to it. Used by the map
// command and the main menu
void RequestMap(const std::string &path);

// A level's game data path from its name: "platforms", "platforms.json" and "maps/platforms.json" all
// give "maps/platforms.json"
std::string MapPath(const std::string &name);
