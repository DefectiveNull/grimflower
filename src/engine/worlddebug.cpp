#include "worlddebug.h"
#include "cvars.h"
#include "raymath.h"

std::vector<WorldEntity> GetWorldEntities(const World &world)
{
    std::vector<WorldEntity> entities;
    entities.push_back({ "sun", world.lighting.sun.position });
    for (size_t i = 0; i < world.lighting.lights.size(); i++)
    {
        entities.push_back({ "light " + std::to_string(i), world.lighting.lights[i].position });
    }
    return entities;
}

void DrawLightMarkers(const Lighting &lighting)
{
    if (r_showentities.value == 0.0f) return;

    // Sun, with a line showing which way the light goes
    const Sun &sun = lighting.sun;
    DrawSphere(sun.position, 16.0f, YELLOW);
    DrawLine3D(sun.position, Vector3Add(sun.position, Vector3Scale(Vector3Normalize(sun.direction), 96.0f)), ORANGE);

    // Point lights, in their own color, with a wireframe sphere showing their radius
    for (const PointLight &light : lighting.lights)
    {
        DrawSphere(light.position, 5.0f, light.color);
        DrawSphereWires(light.position, light.radius, 8, 8, Fade(light.color, 0.3f));
    }
}

void DrawWorldEntityLabels(const World &world, Camera camera)
{
    if (r_showentities.value == 0.0f) return;

    const int fontSize = 20;
    const int padding = 4;
    Vector3 forward = Vector3Subtract(camera.target, camera.position);

    for (const WorldEntity &entity : GetWorldEntities(world))
    {
        // Skip entities behind the camera, which would otherwise get projected onto the screen mirrored
        if (Vector3DotProduct(Vector3Subtract(entity.position, camera.position), forward) <= 0.0f) continue;

        Vector2 screen = GetWorldToScreen(entity.position, camera);
        const char *label = TextFormat("%s (%.1f %.1f %.1f)", entity.name.c_str(), entity.position.x, entity.position.y, entity.position.z);
        int width = MeasureText(label, fontSize);
        int x = (int)screen.x - width/2;
        int y = (int)screen.y - fontSize - 16;  // Just above the marker

        DrawRectangle(x - padding, y - padding, width + 2*padding, fontSize + 2*padding, Fade(BLACK, 0.6f));
        DrawText(label, x, y, fontSize, YELLOW);
    }
}
