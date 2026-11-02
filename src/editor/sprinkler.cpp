// The Sprinkler: for dressing a level with lots of small props, like grass and rocks. Each click scatters
// prop_models picked at random from a short list around the mouse, each resting on the surface under it and
// tipped to match. They're ordinary entities, so everything else about them works as usual afterwards

#include "editor.h"
#include "editinput.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <vector>

static const int ATTEMPTS_PER_PROP = 4;     // Spots that miss the surface are tried again this many times, for a full count on uneven ground
static const float MIN_FACING = 0.25f;      // Surfaces turned further than this from the clicked one (a dot product) get nothing: walls next to a floor, or the inside of something solid

static float RandomUnit()
{
    return GetRandomValue(0, 10000)/10000.0f;
}

void ToggleSprinkler(EditorState &editor)
{
    SprinklerState &s = editor.sprinkler;
    if (editor.mode != MODE_OBJECT) return;

    s.active = !s.active;
    s.hovering = false;
    if (!s.active)
    {
        SetStatus(editor, "Sprinkler off");
        return;
    }
    editor.selected = -1;
    editor.dragging = false;
    SetStatus(editor, "Sprinkler: pick up to three props in the Details tab, then click on the level. Esc stops");
}

// Rounded so the level file doesn't fill up with long decimals
static double Round(float value, float places)
{
    return roundf(value*places)/(double)places;
}

int SprinkleProps(EditorState &editor, Vector3 center, Vector3 normal)
{
    const SprinklerState &s = editor.sprinkler;
    std::vector<std::string> props;
    for (const std::string &prop : s.props) if (!prop.empty()) props.push_back(prop);
    if (props.empty())
    {
        SetStatus(editor, "Pick at least one prop in the Details tab first");
        return 0;
    }

    // Spots are picked on the plane of the click, then dropped onto whatever's there, from high enough to reach
    // slopes up to 45 degrees and the same below. Further than that, like off the edge of a ledge, they miss
    Vector3 across = Vector3Normalize(Vector3Perpendicular(normal));
    Vector3 along = Vector3CrossProduct(normal, across);
    float reach = fmaxf(s.radius, 1.0f);

    std::vector<LevelEntity> placed;
    for (int attempt = 0; attempt < s.count*ATTEMPTS_PER_PROP && (int)placed.size() < s.count; attempt++)
    {
        float distance = s.radius*sqrtf(RandomUnit());  // The square root spreads them evenly, not bunched up in the middle
        float turn = RandomUnit()*2.0f*PI;
        Vector3 spot = Vector3Add(center, Vector3Add(Vector3Scale(across, distance*cosf(turn)), Vector3Scale(along, distance*sinf(turn))));

        Ray ray = { Vector3Add(spot, Vector3Scale(normal, reach)), Vector3Negate(normal) };
        Vector3 point, surface;
        if (!RaycastWorld(editor, ray, point, &surface)) continue;
        if (Vector3Distance(point, ray.position) > 2.0f*reach) continue;
        if (Vector3DotProduct(surface, normal) < MIN_FACING) continue;

        LevelEntity entity;
        entity.classname = "prop_model";
        entity.origin = { (float)Round(point.x, 100.0f), (float)Round(point.y, 100.0f), (float)Round(point.z, 100.0f) };
        float scale = std::min(s.scaleMin, s.scaleMax) + RandomUnit()*fabsf(s.scaleMax - s.scaleMin);
        entity.properties = nlohmann::ordered_json::object();
        entity.properties["model"] = props[GetRandomValue(0, (int)props.size() - 1)];
        entity.properties["angle"] = GetRandomValue(0, 359);
        entity.properties["scale"] = Round(scale, 100.0f);
        entity.properties["up"] = { Round(surface.x, 10000.0f), Round(surface.y, 10000.0f), Round(surface.z, 10000.0f) };
        entity.properties["static"] = s.isStatic;
        entity.properties["collision"] = s.collision;
        entity.properties["detailLighting"] = s.detailLighting;
        placed.push_back(entity);
    }

    if (placed.empty())
    {
        SetStatus(editor, "Nowhere to put props there");
        return 0;
    }
    RecordUndo(editor, "Sprinkle props");
    editor.level.entities.insert(editor.level.entities.end(), placed.begin(), placed.end());
    SetStatus(editor, "Sprinkled %d props", (int)placed.size());
    return (int)placed.size();
}

void UpdateSprinkler(EditorState &editor, bool mouseFree)
{
    SprinklerState &s = editor.sprinkler;
    Ray ray = GetViewportMouseRay(editor);
    Vector3 point, normal;

    // The back of a face isn't a surface to put things on, like from inside something solid
    s.hovering = mouseFree && RaycastWorld(editor, ray, point, &normal) && Vector3DotProduct(normal, ray.direction) < 0.0f;
    if (s.hovering)
    {
        s.hoverPoint = point;
        s.hoverNormal = normal;
    }

    if (!mouseFree || !EdLeftPressed()) return;
    if (s.hovering) SprinkleProps(editor, s.hoverPoint, s.hoverNormal);
    else SetStatus(editor, "That's empty space, click on the level to sprinkle props on it");
}

// The ring a click would fill, lying on the surface
void DrawSprinklerOverlay(const EditorState &editor)
{
    const SprinklerState &s = editor.sprinkler;
    if (!s.active || !s.hovering) return;

    Vector3 across = Vector3Normalize(Vector3Perpendicular(s.hoverNormal));
    Vector3 along = Vector3CrossProduct(s.hoverNormal, across);
    Vector3 center = Vector3Add(s.hoverPoint, Vector3Scale(s.hoverNormal, 0.5f));  // Just off the surface, so it doesn't flicker into it
    auto onRing = [&](int step)
    {
        float turn = step*2.0f*PI/64.0f;
        return Vector3Add(center, Vector3Add(Vector3Scale(across, s.radius*cosf(turn)), Vector3Scale(along, s.radius*sinf(turn))));
    };
    for (int step = 0; step < 64; step++) DrawLine3D(onRing(step), onRing(step + 1), YELLOW);
    DrawLine3D(s.hoverPoint, Vector3Add(s.hoverPoint, Vector3Scale(s.hoverNormal, 16.0f)), YELLOW);
}
