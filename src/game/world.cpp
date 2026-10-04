#include "world.h"
#include "cvars.h"
#include "gamedata.h"
#include "raymath.h"
#include "rlgl.h"

#include <cfloat>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

// The prebuilt raylib has TGA loading compiled out, so decode TGAs ourselves.
// Static so it can't clash with raylib's own internal copy of stb_image
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_TGA
#include "stb_image.h"

// Distance a trace stops short of a surface, so the box never ends up exactly touching it (Quake's DIST_EPSILON)
static const float DIST_EPSILON = 0.001f;

// Tolerances for treating two planes as the same, and for the convexity check
static const float NORMAL_EPSILON = 0.0001f;
static const float PLANE_DIST_EPSILON = 0.001f;

// The rest of a line after its tag, without surrounding whitespace. Also drops the '\r' Windows line endings leave behind
static std::string RestOfLine(std::istringstream &tokens)
{
    std::string rest;
    std::getline(tokens >> std::ws, rest);
    while (!rest.empty() && isspace((unsigned char)rest.back())) rest.pop_back();
    return rest;
}

// Loads the TGA diffuse textures from the OBJ's material file and assigns them to the model's materials
static void LoadTgaTextures(Model &model, const char *objPath)
{
    std::string objDir = GetDirectoryPath(objPath);

    char *objText = LoadFileText(objPath);
    if (objText == nullptr) return;
    std::istringstream objFile(objText);
    UnloadFileText(objText);

    std::string line, mtlName;
    while (std::getline(objFile, line))
    {
        std::istringstream tokens(line);
        std::string tag;
        tokens >> tag;
        if (tag == "mtllib") { mtlName = RestOfLine(tokens); break; }
    }
    if (mtlName.empty()) return;

    char *mtlText = LoadFileText((objDir + "/" + mtlName).c_str());
    if (mtlText == nullptr) return;
    std::istringstream mtlFile(mtlText);
    UnloadFileText(mtlText);

    // Diffuse texture path of each material, in file order
    std::vector<std::string> texturePaths;
    while (std::getline(mtlFile, line))
    {
        std::istringstream tokens(line);
        std::string tag;
        tokens >> tag;
        if (tag == "newmtl") texturePaths.emplace_back();
        else if (tag == "map_Kd" && !texturePaths.empty()) texturePaths.back() = RestOfLine(tokens);
    }

    // raylib may put a default material in front of the OBJ's materials
    int offset = model.materialCount - (int)texturePaths.size();
    if (offset < 0) return;

    for (size_t i = 0; i < texturePaths.size(); i++)
    {
        if (texturePaths[i].empty() || !IsFileExtension(texturePaths[i].c_str(), ".tga")) continue;

        // Map editors write texture paths relative to the game data root, like Quake. Fall back to
        // relative to the OBJ, which is how raylib resolves them
        std::string path = texturePaths[i];
        if (!GameDataFileExists(path.c_str())) path = objDir + "/" + texturePaths[i];

        // Read through raylib so it comes from gamedata.zip, then decode from memory
        int fileSize = 0;
        unsigned char *fileData = LoadFileData(path.c_str(), &fileSize);
        if (fileData == nullptr) continue;

        int width, height, channels;
        unsigned char *pixels = stbi_load_from_memory(fileData, fileSize, &width, &height, &channels, 4);
        UnloadFileData(fileData);
        if (pixels == nullptr)
        {
            TraceLog(LOG_WARNING, "WORLD: [%s] Failed to load texture: %s", path.c_str(), stbi_failure_reason());
            continue;
        }

        Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        Texture texture = LoadTextureFromImage(image);
        stbi_image_free(pixels);

        Material &material = model.materials[offset + i];
        if (material.maps[MATERIAL_MAP_DIFFUSE].texture.id != rlGetTextureIdDefault()) UnloadTexture(material.maps[MATERIAL_MAP_DIFFUSE].texture);
        material.maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        TraceLog(LOG_INFO, "WORLD: [%s] Loaded texture (%dx%d)", path.c_str(), width, height);
    }
}

// Box around every brush in the level
static BoundingBox ComputeBounds(const std::vector<Brush> &colliders)
{
    BoundingBox bounds = { { FLT_MAX, FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX, -FLT_MAX } };
    for (const Brush &brush : colliders)
    {
        for (const std::vector<Vector3> &face : brush.faces)
        {
            for (Vector3 p : face)
            {
                bounds.min = Vector3Min(bounds.min, p);
                bounds.max = Vector3Max(bounds.max, p);
            }
        }
    }
    return bounds;
}

bool LoadWorld(World &world, const char *objPath, float scale)
{
    world.colliders = ComputeColliders(objPath, scale);
    if (world.colliders.empty()) return false;
    world.bounds = ComputeBounds(world.colliders);

    world.model = LoadModel(objPath);
    world.model.transform = MatrixScale(scale, scale, scale);
    LoadTgaTextures(world.model, objPath);

    InitLighting(world.lighting, world.bounds);
    for (int i = 0; i < world.model.materialCount; i++) world.model.materials[i].shader = world.lighting.shader;
    return true;
}

void UnloadWorld(World &world)
{
    // UnloadModel leaves shaders alone, since they can be shared between models
    UnloadModel(world.model);
    UnloadLighting(world.lighting);
    UnloadSkybox(world.skybox);
    world.colliders.clear();
}

void RenderWorldShadows(World &world)
{
    RenderShadowMap(world.lighting, world.model);
}

void DrawWorld(const World &world, bool showColliders)
{
    BeginLighting(world.lighting);
        DrawModel(world.model, { 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
        DrawModelWires(world.model, { 0.0f, 0.0f, 0.0f }, 1.0f, Fade(BLACK, 0.6f));
    EndLighting();

    DrawLightMarkers(world.lighting);

    if (showColliders)
    {
        for (const Brush &brush : world.colliders)
        {
            for (const std::vector<Vector3> &face : brush.faces)
            {
                for (size_t i = 0; i < face.size(); i++) DrawLine3D(face[i], face[(i + 1)%face.size()], LIME);
            }
        }
    }
}

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

static bool HasPlane(const Brush &brush, Vector3 normal, float dist)
{
    for (const Plane &plane : brush.planes)
    {
        if (Vector3DotProduct(plane.normal, normal) > 1.0f - NORMAL_EPSILON && fabsf(plane.dist - dist) < PLANE_DIST_EPSILON) return true;
    }
    return false;
}

// Adds the plane with this normal that touches the brush from outside, unless the brush already has it
static void AddSupportingPlane(Brush &brush, const std::vector<Vector3> &points, Vector3 normal)
{
    float dist = -FLT_MAX;
    for (Vector3 p : points) dist = fmaxf(dist, Vector3DotProduct(normal, p));

    if (!HasPlane(brush, normal, dist)) brush.planes.push_back({ normal, dist });
}

static Brush BuildBrush(const std::vector<std::vector<Vector3>> &faces, int objectIndex)
{
    Brush brush;
    brush.faces = faces;

    std::vector<Vector3> points;
    for (const std::vector<Vector3> &face : faces) points.insert(points.end(), face.begin(), face.end());

    Vector3 centroid = { 0 };
    for (Vector3 p : points) centroid = Vector3Add(centroid, p);
    centroid = Vector3Scale(centroid, 1.0f/points.size());

    // Face planes. Newell's method gives a stable normal even for slightly non-planar polygons
    for (const std::vector<Vector3> &face : faces)
    {
        Vector3 normal = { 0 };
        for (size_t i = 0; i < face.size(); i++)
        {
            Vector3 a = face[i];
            Vector3 b = face[(i + 1)%face.size()];
            normal.x += (a.y - b.y)*(a.z + b.z);
            normal.y += (a.z - b.z)*(a.x + b.x);
            normal.z += (a.x - b.x)*(a.y + b.y);
        }
        if (Vector3LengthSqr(normal) == 0.0f) continue;  // Degenerate face
        normal = Vector3Normalize(normal);

        float dist = Vector3DotProduct(normal, face[0]);
        if (Vector3DotProduct(normal, centroid) > dist)
        {
            // Face is wound inward, flip it so the normal points out of the brush
            normal = Vector3Negate(normal);
            dist = -dist;
        }

        if (!HasPlane(brush, normal, dist)) brush.planes.push_back({ normal, dist });
    }

    bool convex = true;
    for (const Plane &plane : brush.planes)
    {
        for (Vector3 p : points)
        {
            if (Vector3DotProduct(plane.normal, p) - plane.dist > PLANE_DIST_EPSILON) convex = false;
        }
    }
    if (!convex) TraceLog(LOG_WARNING, "WORLD: Object %d is not convex, its collision will be wrong", objectIndex);

    // Bevel planes. Expanding a brush by a box also rounds off its corners and edges into new flat faces;
    // without these extra planes, a box sliding past a sharp edge would catch on empty space
    const Vector3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for (Vector3 axis : axes)
    {
        AddSupportingPlane(brush, points, axis);
        AddSupportingPlane(brush, points, Vector3Negate(axis));
    }

    for (const std::vector<Vector3> &face : faces)
    {
        for (size_t i = 0; i < face.size(); i++)
        {
            Vector3 edge = Vector3Subtract(face[(i + 1)%face.size()], face[i]);
            for (Vector3 axis : axes)
            {
                Vector3 normal = Vector3CrossProduct(edge, axis);
                if (Vector3LengthSqr(normal) < 1e-8f) continue;  // Edge is parallel to this axis
                normal = Vector3Normalize(normal);

                AddSupportingPlane(brush, points, normal);
                AddSupportingPlane(brush, points, Vector3Negate(normal));
            }
        }
    }

    return brush;
}

std::vector<Brush> ComputeColliders(const char *objPath, float scale)
{
    std::vector<Brush> brushes;

    char *text = LoadFileText(objPath);
    if (text == nullptr) return brushes;
    std::istringstream file(text);
    UnloadFileText(text);

    std::vector<Vector3> vertices;
    std::vector<std::vector<std::vector<int>>> objects;  // Faces (as vertex indices) of each object

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream tokens(line);
        std::string tag;
        tokens >> tag;

        if (tag == "v")
        {
            Vector3 v = { 0 };
            tokens >> v.x >> v.y >> v.z;
            vertices.push_back(Vector3Scale(v, scale));
        }
        else if (tag == "o" || tag == "g")
        {
            objects.emplace_back();
        }
        else if (tag == "f")
        {
            if (objects.empty()) objects.emplace_back();

            // Face entries look like "v", "v/vt", "v//vn" or "v/vt/vn"; only the position index matters
            std::vector<int> face;
            std::string entry;
            while (tokens >> entry)
            {
                int index = std::stoi(entry);
                face.push_back((index < 0) ? (int)vertices.size() + index : index - 1);
            }
            objects.back().push_back(face);
        }
    }

    for (size_t i = 0; i < objects.size(); i++)
    {
        std::vector<std::vector<Vector3>> faces;
        for (const std::vector<int> &indices : objects[i])
        {
            std::vector<Vector3> face;
            for (int index : indices)
            {
                if (index >= 0 && index < (int)vertices.size()) face.push_back(vertices[index]);
            }
            if (face.size() >= 3) faces.push_back(face);
        }

        if (!faces.empty()) brushes.push_back(BuildBrush(faces, (int)i));
    }

    TraceLog(LOG_INFO, "WORLD: [%s] Computed %d collision brushes", objPath, (int)brushes.size());
    return brushes;
}

// Clips a moving box against one convex brush (Quake 2's CM_ClipBoxToBrush)
static void ClipBoxToBrush(Trace &trace, const Brush &brush, Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs)
{
    float enterFraction = -1.0f;
    float leaveFraction = 1.0f;
    const Plane *clipPlane = nullptr;
    bool startOut = false;

    // Shallowest plane the start point is behind, for recovering from tiny float-error penetrations
    float shallowestDepth = -FLT_MAX;
    const Plane *shallowestPlane = nullptr;
    bool shallowestMovingIn = false;

    for (const Plane &plane : brush.planes)
    {
        // Push the plane out by the box corner that would touch it first
        Vector3 offset = {
            (plane.normal.x < 0.0f) ? maxs.x : mins.x,
            (plane.normal.y < 0.0f) ? maxs.y : mins.y,
            (plane.normal.z < 0.0f) ? maxs.z : mins.z
        };
        float dist = plane.dist - Vector3DotProduct(offset, plane.normal);

        float d1 = Vector3DotProduct(start, plane.normal) - dist;
        float d2 = Vector3DotProduct(end, plane.normal) - dist;

        if (d1 > 0.0f) startOut = true;

        // Entirely in front of this plane, so the move can't touch the brush. Like Quake 1, only actually crossing
        // the plane counts as a hit: DIST_EPSILON is just for backing off, otherwise a box resting EPSILON above a
        // slope gets stuck on it when moving parallel to it
        if (d1 > 0.0f && d2 >= 0.0f) return;

        if (d1 <= 0.0f && d1 > shallowestDepth)
        {
            shallowestDepth = d1;
            shallowestPlane = &plane;
            shallowestMovingIn = (d2 < d1 && d2 < -DIST_EPSILON);
        }

        // Entirely behind this plane, it doesn't limit the move
        if (d1 <= 0.0f && d2 <= 0.0f) continue;

        if (d1 > d2)
        {
            // Entering the brush through this plane
            float f = (d1 - DIST_EPSILON)/(d1 - d2);
            if (f > enterFraction)
            {
                enterFraction = f;
                clipPlane = &plane;
            }
        }
        else
        {
            // Leaving the brush through this plane
            float f = (d1 + DIST_EPSILON)/(d1 - d2);
            if (f < leaveFraction) leaveFraction = f;
        }
    }

    if (!startOut)
    {
        // Barely inside from float error and moving clearly deeper: treat it as touching the surface.
        // Anything deeper means we're stuck in it, so let the box move out freely
        if (shallowestPlane != nullptr && shallowestDepth > -DIST_EPSILON && shallowestMovingIn)
        {
            trace.fraction = 0.0f;
            trace.normal = shallowestPlane->normal;
        }
        else
        {
            trace.startSolid = true;
        }
        return;
    }

    if (clipPlane != nullptr && enterFraction < leaveFraction && enterFraction > -1.0f && enterFraction < trace.fraction)
    {
        trace.fraction = fmaxf(0.0f, enterFraction);
        trace.normal = clipPlane->normal;
    }
}

Trace TraceBox(const World &world, Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs)
{
    Trace trace = { 1.0f, end, { 0.0f, 0.0f, 0.0f }, false };

    for (const Brush &brush : world.colliders) ClipBoxToBrush(trace, brush, start, end, mins, maxs);

    trace.endPos = Vector3Lerp(start, end, trace.fraction);
    return trace;
}
