#include "levellights.h"
#include "entitylist.h"
#include "raymath.h"

// The game spawns these itself (src/game/entities.cpp); this copies what it does, so the editor shows
// the same lighting. Missing parameters get their defaults from entitylist.txt, same as the game's
void ApplyLevelLights(const Level &level, Lighting &lighting, const Sun &defaultSun)
{
    // A "sun" entity replaces the default sun, the first one if there are several
    lighting.sun = defaultSun;
    lighting.lights.clear();
    bool hasSun = false;

    for (const LevelEntity &entity : level.entities)
    {
        if (entity.classname == "sun" && !hasSun)
        {
            Sun &sun = lighting.sun;
            sun.position = entity.origin;
            sun.direction = Vector3Normalize(EntityVectorOrDefault(entity, "direction", sun.direction));
            sun.color = EntityColorOrDefault(entity, "color", sun.color);
            sun.intensity = EntityNumberOrDefault(entity, "intensity", sun.intensity);
            sun.ambient = EntityNumberOrDefault(entity, "ambient", sun.ambient);
            hasSun = true;
        }
        else if (entity.classname == "light")
        {
            PointLight light;
            light.position = entity.origin;
            light.color = EntityColorOrDefault(entity, "color", WHITE);
            light.intensity = EntityNumberOrDefault(entity, "intensity", 1.0f);
            light.radius = EntityNumberOrDefault(entity, "radius", 5.0f);
            lighting.lights.push_back(light);
        }
    }
}
