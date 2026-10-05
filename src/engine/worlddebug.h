#pragma once

// The r_showentities overlay: markers for the sun and point lights, and labels with their names and positions

#include "raylib.h"
#include "world.h"

#include <string>
#include <vector>

// Something placed in the world, for the r_showentities labels
struct WorldEntity
{
    std::string name;
    Vector3 position;
};

// Every entity in the world: the sun and the point lights
std::vector<WorldEntity> GetWorldEntities(const World &world);

// With r_showentities on, draws a marker for the sun and each point light. Call inside BeginMode3D
void DrawLightMarkers(const Lighting &lighting);

// With r_showentities on, draws a name and position label over each entity. Call after EndMode3D
void DrawWorldEntityLabels(const World &world, Camera camera);
