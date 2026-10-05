#include "world.h"
#include "gamedata.h"
#include "raymath.h"
#include "rlgl.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// The prebuilt raylib has TGA loading compiled out, so decode TGAs ourselves (LoadWorldTexture).
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

// A material from the OBJ's .mtl file. Only the diffuse texture is used
struct ObjMaterial
{
    std::string name;
    std::string texturePath;  // map_Kd, empty if it has none
};

// Reads the materials from a .mtl file, through the game data so it can come from gamedata.zip
static void ReadMtl(const std::string &path, std::vector<ObjMaterial> &materials)
{
    char *text = LoadFileText(path.c_str());
    if (text == nullptr)
    {
        TraceLog(LOG_WARNING, "WORLD: [%s] Couldn't read the material file, its faces will be untextured", path.c_str());
        return;
    }
    std::istringstream file(text);
    UnloadFileText(text);

    std::string line;
    bool inMaterial = false;  // Lines before the first newmtl don't belong to anything
    while (std::getline(file, line))
    {
        std::istringstream tokens(line);
        std::string tag;
        tokens >> tag;
        if (tag == "newmtl")
        {
            materials.push_back({ RestOfLine(tokens), "" });
            inMaterial = true;
        }
        else if (tag == "map_Kd" && inMaterial)
        {
            materials.back().texturePath = RestOfLine(tokens);
        }
    }
}

// Loads a material's texture through the game data. Returns false if it couldn't
static bool LoadWorldTexture(const std::string &texturePath, const std::string &objDir, Texture &texture)
{
    // Map editors write texture paths relative to the game data root, like Quake. Fall back to
    // relative to the OBJ, which is how most exporters write them
    std::string path = texturePath;
    if (!GameDataFileExists(path.c_str())) path = objDir + "/" + texturePath;

    int fileSize = 0;
    unsigned char *fileData = LoadFileData(path.c_str(), &fileSize);
    if (fileData == nullptr) return false;

    Image image = { 0 };
    unsigned char *pixels = nullptr;
    if (IsFileExtension(path.c_str(), ".tga"))
    {
        // The prebuilt raylib can't read TGAs, so stb_image does
        int channels;
        pixels = stbi_load_from_memory(fileData, fileSize, &image.width, &image.height, &channels, 4);
        if (pixels == nullptr) TraceLog(LOG_WARNING, "WORLD: [%s] Failed to load texture: %s", path.c_str(), stbi_failure_reason());
        image = { pixels, image.width, image.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    }
    else
    {
        image = LoadImageFromMemory(GetFileExtension(path.c_str()), fileData, fileSize);
    }
    UnloadFileData(fileData);
    if (image.data == nullptr) return false;

    texture = LoadTextureFromImage(image);
    if (pixels != nullptr) stbi_image_free(pixels);
    else UnloadImage(image);

    TraceLog(LOG_INFO, "WORLD: [%s] Loaded texture (%dx%d)", path.c_str(), texture.width, texture.height);
    return true;
}

// One corner of an OBJ face: indices into the position, texture coordinate and normal lists, -1 if missing
struct ObjCorner
{
    int position;
    int texcoord;
    int normal;
};

// OBJ indices count from 1, or back from the end of the list if negative. 0 means there isn't one
static int ResolveObjIndex(const std::string &text, size_t count)
{
    if (text.empty()) return -1;
    int index = atoi(text.c_str());
    if (index == 0) return -1;
    index = (index < 0) ? (int)count + index : index - 1;
    return (index >= 0 && index < (int)count) ? index : -1;
}

// Corners look like "v", "v/vt", "v//vn" or "v/vt/vn"
static ObjCorner ParseObjCorner(const std::string &entry, size_t positionCount, size_t texcoordCount, size_t normalCount)
{
    size_t slash1 = entry.find('/');
    size_t slash2 = (slash1 == std::string::npos) ? std::string::npos : entry.find('/', slash1 + 1);

    ObjCorner corner;
    corner.position = ResolveObjIndex(entry.substr(0, slash1), positionCount);
    corner.texcoord = (slash1 == std::string::npos) ? -1 : ResolveObjIndex(entry.substr(slash1 + 1, slash2 - slash1 - 1), texcoordCount);
    corner.normal = (slash2 == std::string::npos) ? -1 : ResolveObjIndex(entry.substr(slash2 + 1), normalCount);
    return corner;
}

// The triangles of one material, with every corner written out in full (no index buffer)
struct MeshData
{
    std::vector<float> vertices;
    std::vector<float> texcoords;
    std::vector<float> normals;
};

static Mesh UploadMeshData(const MeshData &data)
{
    Mesh mesh = { 0 };
    mesh.vertexCount = (int)data.vertices.size()/3;
    mesh.triangleCount = mesh.vertexCount/3;

    // UnloadModel frees these with RL_FREE
    mesh.vertices = (float *)RL_MALLOC(data.vertices.size()*sizeof(float));
    mesh.texcoords = (float *)RL_MALLOC(data.texcoords.size()*sizeof(float));
    mesh.normals = (float *)RL_MALLOC(data.normals.size()*sizeof(float));
    memcpy(mesh.vertices, data.vertices.data(), data.vertices.size()*sizeof(float));
    memcpy(mesh.texcoords, data.texcoords.data(), data.texcoords.size()*sizeof(float));
    memcpy(mesh.normals, data.normals.data(), data.normals.size()*sizeof(float));

    UploadMesh(&mesh, false);
    return mesh;
}

// Loads the world model with one mesh per material, from the OBJ's usemtl lines. raylib's own OBJ
// loader reads the .mtl straight from disk, so it couldn't tell faces apart when running from gamedata.zip
static Model LoadWorldModel(const char *objPath)
{
    Model model = { 0 };
    model.transform = MatrixIdentity();
    std::string objDir = GetDirectoryPath(objPath);

    char *text = LoadFileText(objPath);
    if (text == nullptr) return model;
    std::istringstream file(text);
    UnloadFileText(text);

    std::vector<Vector3> positions;
    std::vector<Vector2> texcoords;
    std::vector<Vector3> normals;

    // Slot 0 is for faces before any usemtl, or with a material the .mtl doesn't have
    std::vector<ObjMaterial> materials = { { "", "" } };
    std::vector<MeshData> meshData(1);
    int currentMaterial = 0;

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
            positions.push_back(v);
        }
        else if (tag == "vt")
        {
            Vector2 vt = { 0 };
            tokens >> vt.x >> vt.y;
            texcoords.push_back({ vt.x, 1.0f - vt.y });  // OBJ's v goes up, textures go down
        }
        else if (tag == "vn")
        {
            Vector3 vn = { 0 };
            tokens >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        }
        else if (tag == "mtllib")
        {
            ReadMtl(objDir + "/" + RestOfLine(tokens), materials);
            meshData.resize(materials.size());
        }
        else if (tag == "usemtl")
        {
            std::string name = RestOfLine(tokens);
            currentMaterial = 0;
            for (size_t i = 1; i < materials.size(); i++)
            {
                if (materials[i].name == name) currentMaterial = (int)i;
            }
            if (currentMaterial == 0) TraceLog(LOG_WARNING, "WORLD: [%s] Material \"%s\" isn't in the .mtl file, its faces will be untextured", objPath, name.c_str());
        }
        else if (tag == "f")
        {
            std::vector<ObjCorner> corners;
            std::string entry;
            while (tokens >> entry) corners.push_back(ParseObjCorner(entry, positions.size(), texcoords.size(), normals.size()));

            bool valid = corners.size() >= 3;
            for (const ObjCorner &corner : corners) valid = valid && corner.position >= 0;
            if (!valid) continue;

            // Split the polygon into a fan of triangles, like the exporter's faces are convex
            MeshData &data = meshData[currentMaterial];
            for (size_t i = 1; i + 1 < corners.size(); i++)
            {
                const ObjCorner triangle[3] = { corners[0], corners[i], corners[i + 1] };

                // Without normals in the file, use the triangle's own
                Vector3 a = positions[triangle[0].position], b = positions[triangle[1].position], c = positions[triangle[2].position];
                Vector3 flatNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));

                for (const ObjCorner &corner : triangle)
                {
                    Vector3 p = positions[corner.position];
                    Vector2 uv = (corner.texcoord >= 0) ? texcoords[corner.texcoord] : Vector2{ 0.0f, 0.0f };
                    Vector3 n = (corner.normal >= 0) ? normals[corner.normal] : flatNormal;
                    data.vertices.insert(data.vertices.end(), { p.x, p.y, p.z });
                    data.texcoords.insert(data.texcoords.end(), { uv.x, uv.y });
                    data.normals.insert(data.normals.end(), { n.x, n.y, n.z });
                }
            }
        }
    }

    // Every material gets a slot, so mesh material indices match the .mtl. Only the used ones get a texture
    model.materialCount = (int)materials.size();
    model.materials = (Material *)RL_CALLOC(model.materialCount, sizeof(Material));
    for (int i = 0; i < model.materialCount; i++) model.materials[i] = LoadMaterialDefault();

    int meshCount = 0;
    for (const MeshData &data : meshData) meshCount += data.vertices.empty() ? 0 : 1;
    model.meshCount = meshCount;
    model.meshes = (Mesh *)RL_CALLOC(meshCount, sizeof(Mesh));
    model.meshMaterial = (int *)RL_CALLOC(meshCount, sizeof(int));

    int meshIndex = 0;
    for (size_t i = 0; i < meshData.size(); i++)
    {
        if (meshData[i].vertices.empty()) continue;

        model.meshes[meshIndex] = UploadMeshData(meshData[i]);
        model.meshMaterial[meshIndex] = (int)i;
        meshIndex++;

        Texture texture;
        if (!materials[i].texturePath.empty() && LoadWorldTexture(materials[i].texturePath, objDir, texture))
        {
            model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        }
        else if (i > 0)
        {
            TraceLog(LOG_WARNING, "WORLD: [%s] Material \"%s\" has no texture that could be loaded", objPath, materials[i].name.c_str());
        }
    }

    TraceLog(LOG_INFO, "WORLD: [%s] Loaded %d meshes, one per material used", objPath, meshCount);
    return model;
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

    world.model = LoadWorldModel(objPath);
    world.model.transform = MatrixScale(scale, scale, scale);

    InitLighting(world.lighting, world.bounds);
    for (int i = 0; i < world.model.materialCount; i++) world.model.materials[i].shader = world.lighting.shader;
    return true;
}

void UnloadWorld(World &world)
{
    // UnloadModel leaves shaders and textures alone, since they can be shared between models.
    // The world's textures are its own, so unload them first, or every reload would leak them
    for (int i = 0; i < world.model.materialCount; i++)
    {
        Texture texture = world.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
        if (texture.id != rlGetTextureIdDefault()) UnloadTexture(texture);
    }
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
