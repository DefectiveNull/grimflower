#pragma once

// The level's lights, for the editor's viewport. The game spawns them from its entity table instead

#include "level.h"
#include "lighting.h"

// Rebuilds the sun and point lights from the level's entities. The editor calls this every frame,
// so edits to light entities show up right away. Without a "sun" entity, defaultSun is used
void ApplyLevelLights(const Level &level, Lighting &lighting, const Sun &defaultSun);
