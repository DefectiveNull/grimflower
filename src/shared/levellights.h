#pragma once

// Turns a level's light entities ("sun" and "light") into the sun and point lights. The game uses it
// when spawning a level and the editor for its preview, so both light a level the same way. A new kind
// of light goes here, plus its parameters in the entity table in src/game/entities.cpp

#include "level.h"
#include "lighting.h"

// Reads an entity's parameter, or that parameter's default if the level file doesn't set it. The game
// takes defaults from its entity table, the editor from entitylist.txt
struct EntityParamReader
{
    float (*number)(const LevelEntity &entity, const char *name);
    Vector3 (*vector)(const LevelEntity &entity, const char *name);
    Color (*color)(const LevelEntity &entity, const char *name);
    bool (*boolean)(const LevelEntity &entity, const char *name);
};

// Rebuilds the sun and point lights from the level's light entities. The first "sun" replaces
// defaultSun, and any after it are ignored. skyColor is the skybox's average color (Skybox::averageColor), for a
// sun that takes its ambient light from the sky. Returns how many suns were ignored
int ApplyLevelLights(const Level &level, Lighting &lighting, Sun defaultSun, const EntityParamReader &params, Vector3 skyColor);
