#include "levellights.h"
#include "raymath.h"

int ApplyLevelLights(const Level &level, Lighting &lighting, Sun defaultSun, const EntityParamReader &params)
{
    lighting.sun = defaultSun;
    lighting.lights.clear();
    bool hasSun = false;
    int ignoredSuns = 0;

    for (const LevelEntity &entity : level.entities)
    {
        if (entity.classname == "sun")
        {
            if (hasSun)
            {
                ignoredSuns++;
                continue;
            }

            Sun &sun = lighting.sun;
            sun.position = entity.origin;
            sun.direction = Vector3Normalize(params.vector(entity, "direction"));
            sun.color = params.color(entity, "color");
            sun.intensity = params.number(entity, "intensity");
            sun.ambient = params.number(entity, "ambient");
            hasSun = true;
        }
        else if (entity.classname == "light")
        {
            PointLight light;
            light.position = entity.origin;
            light.color = params.color(entity, "color");
            light.intensity = params.number(entity, "intensity");
            light.radius = params.number(entity, "radius");
            lighting.lights.push_back(light);
        }
    }

    return ignoredSuns;
}
