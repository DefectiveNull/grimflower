#include "world.h"
#include "bounce.h"
#include "gamedata.h"
#include "modelfile.h"
#include "raymath.h"
#include "rlgl.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The prebuilt raylib has TGA loading compiled out, so decode TGAs ourselves (LoadWorldTexture). PNG, JPEG
// and BMP too, for reading texture sizes without loading them (GetGameTextureSize). Static so it can't clash
// with raylib's own internal copy of stb_image
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_TGA
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"

// Distance a trace stops short of a surface, so the box never ends up exactly touching it (Quake's DIST_EPSILON)
static const float DIST_EPSILON = 0.03125f;

// Tolerances for treating two planes as the same, and for the convexity check
static const float NORMAL_EPSILON = 0.0001f;
static const float PLANE_DIST_EPSILON = 0.03125f;

// Makes a texture from an image file in memory. A format of "" means it could be any image file
static bool DecodeWorldTexture(const unsigned char *fileData, int fileSize, const char *format, const char *name, Texture &texture)
{
    Image image = { 0 };
    unsigned char *pixels = nullptr;
    if (format[0] == '\0' || TextIsEqual(format, ".tga"))
    {
        // The prebuilt raylib can't read TGAs, so stb_image does. It works out the format itself
        int channels;
        pixels = stbi_load_from_memory(fileData, fileSize, &image.width, &image.height, &channels, 4);
        if (pixels == nullptr) TraceLog(LOG_WARNING, "WORLD: [%s] Failed to load texture: %s", name, stbi_failure_reason());
        image = { pixels, image.width, image.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    }
    else
    {
        image = LoadImageFromMemory(format, fileData, fileSize);
    }
    if (image.data == nullptr) return false;

    texture = LoadTextureFromImage(image);
    if (pixels != nullptr) stbi_image_free(pixels);
    else UnloadImage(image);

    TraceLog(LOG_INFO, "WORLD: [%s] Loaded texture (%dx%d)", name, texture.width, texture.height);
    return true;
}

// Loads a material's texture, from inside the model or through the game data. Returns false if it couldn't
static bool LoadWorldTexture(const ModelMaterial &material, const std::string &modelFolder, Texture &texture)
{
    if (!material.embeddedData.empty())
    {
        return DecodeWorldTexture(material.embeddedData.data(), (int)material.embeddedData.size(), "", material.texturePath.c_str(), texture);
    }

    std::string path = FindModelTexture(material.texturePath, modelFolder);
    if (path.empty()) return false;

    int fileSize = 0;
    unsigned char *fileData = LoadFileData(path.c_str(), &fileSize);
    if (fileData == nullptr) return false;

    bool loaded = DecodeWorldTexture(fileData, fileSize, GetFileExtension(path.c_str()), path.c_str(), texture);
    UnloadFileData(fileData);
    return loaded;
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

// Loads a model with one mesh per material, so each can have its own texture and material maps
static Model LoadWorldModel(const char *modelPath, std::vector<WorldMaterial> &worldMaterials)
{
    Model model = { 0 };
    model.transform = MatrixIdentity();
    std::string modelFolder = GetDirectoryPath(modelPath);

    ModelFile file;
    if (!LoadModelFile(modelPath, false, file)) return model;

    std::vector<MeshData> meshData(file.materials.size());
    for (const ModelObject &object : file.objects)
    {
        for (const ModelFace &face : object.faces)
        {
            if (face.corners.size() != 3) continue;
            Vector3 a = face.corners[0].position, b = face.corners[1].position, c = face.corners[2].position;
            Vector3 flatNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));  // For files without normals

            MeshData &data = meshData[face.material];
            for (const ModelCorner &corner : face.corners)
            {
                Vector3 n = corner.hasNormal ? corner.normal : flatNormal;
                data.vertices.insert(data.vertices.end(), { corner.position.x, corner.position.y, corner.position.z });
                data.texcoords.insert(data.texcoords.end(), { corner.texcoord.x, corner.texcoord.y });
                data.normals.insert(data.normals.end(), { n.x, n.y, n.z });
            }
        }
    }

    // Every material gets a slot, so mesh material indices match the file. Only the used ones get a texture
    model.materialCount = (int)file.materials.size();
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

        if (file.materials[i].texturePath.empty()) continue;
        Texture texture;
        if (LoadWorldTexture(file.materials[i], modelFolder, texture)) model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        else TraceLog(LOG_WARNING, "WORLD: [%s] Couldn't load the texture of material \"%s\"", modelPath, file.materials[i].name.c_str());
    }

    // Each material's diffuse texture names its entry in .materials
    worldMaterials.assign(model.materialCount, WorldMaterial());
    for (int i = 0; i < model.materialCount; i++) worldMaterials[i].diffusePath = file.materials[i].texturePath;

    TraceLog(LOG_INFO, "WORLD: [%s] Loaded %d meshes, one per material used", modelPath, meshCount);
    return model;
}

// ---------------------------------------------------------------------------------------------
// The world, built from the level's geometry

void CreateWorld(World &world)
{
    world.bounds = { { -512.0f, -64.0f, -512.0f }, { 512.0f, 256.0f, 512.0f } };  // Until it has geometry
    InitLighting(world.lighting, world.bounds);
    world.model = { 0 };
    world.model.transform = MatrixIdentity();
    world.hasGeometryBuilt = false;
}

// A diffuse texture, loaded once however many times the geometry is rebuilt. Plain white if it can't be
static Texture CachedTexture(World &world, const std::string &path)
{
    auto found = world.textureCache.find(path);
    if (found != world.textureCache.end()) return found->second;
    Texture texture = { 0 };
    if (path.empty() || !LoadGameTexture(path.c_str(), texture))
    {
        if (!path.empty()) TraceLog(LOG_WARNING, "WORLD: [%s] Couldn't load the texture, its faces will be plain white", path.c_str());
        texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    }
    world.textureCache[path] = texture;
    return texture;
}

void SetWorldGeometry(World &world, const Geometry &geometry, float texelsPerUnit, bool full)
{
    CancelLightmapBake(world);
    if (world.model.meshCount > 0 || world.model.materialCount > 0) UnloadModel(world.model);  // Its textures stay, in the cache
    world.model = { 0 };
    world.model.transform = MatrixIdentity();

    // Every face, triangulated, in world space: one mesh per texture
    std::map<std::string, int> materialIndex;
    std::vector<std::string> materialPaths;
    std::vector<MeshData> meshData;
    BoundingBox bounds = { { FLT_MAX, FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX, -FLT_MAX } };
    for (const GeoMesh &mesh : geometry.meshes)
    {
        if (mesh.shape < 0 || mesh.shape >= (int)geometry.shapes.size()) continue;
        const GeoShape &shape = geometry.shapes[mesh.shape];
        Matrix transform = GeoMeshTransform(mesh);
        for (const GeoFace &face : shape.faces)
        {
            std::vector<Vector3> points = GeoFacePoints(shape, face, transform);
            if (points.size() < 3) continue;
            Vector3 normal = GeoPolygonNormal(points);

            auto found = materialIndex.find(face.material);
            if (found == materialIndex.end())
            {
                found = materialIndex.emplace(face.material, (int)materialPaths.size()).first;
                materialPaths.push_back(face.material);
                meshData.emplace_back();
            }
            Texture texture = CachedTexture(world, face.material);
            MeshData &data = meshData[found->second];
            for (int index : GeoTriangulate(points, normal))
            {
                Vector3 p = points[index];
                Vector2 uv = (face.lockedUVs.size() == points.size()) ? face.lockedUVs[index] : GeoTextureCoord(face, p, texture.width, texture.height, texelsPerUnit);
                data.vertices.insert(data.vertices.end(), { p.x, p.y, p.z });
                data.texcoords.insert(data.texcoords.end(), { uv.x, uv.y });
                data.normals.insert(data.normals.end(), { normal.x, normal.y, normal.z });
            }
            for (Vector3 p : points)
            {
                bounds.min = Vector3Min(bounds.min, p);
                bounds.max = Vector3Max(bounds.max, p);
            }
        }
    }

    Model &model = world.model;
    model.materialCount = (int)materialPaths.size();
    if (model.materialCount > 0)
    {
        model.materials = (Material *)RL_CALLOC(model.materialCount, sizeof(Material));
        model.meshes = (Mesh *)RL_CALLOC(model.materialCount, sizeof(Mesh));
        model.meshMaterial = (int *)RL_CALLOC(model.materialCount, sizeof(int));
    }
    MaterialLibrary library = LoadMaterialLibrary();
    world.materials.clear();
    for (int i = 0; i < model.materialCount; i++)
    {
        model.materials[i] = LoadMaterialDefault();
        model.materials[i].shader = world.lighting.shader;
        model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = CachedTexture(world, materialPaths[i]);
        if (!meshData[i].vertices.empty())
        {
            model.meshMaterial[model.meshCount] = i;
            model.meshes[model.meshCount++] = UploadMeshData(meshData[i]);
        }

        auto cached = world.materialCache.find(materialPaths[i]);
        if (cached == world.materialCache.end()) cached = world.materialCache.emplace(materialPaths[i], LoadWorldMaterial(materialPaths[i], library)).first;
        world.materials.push_back(cached->second);
    }

    // Bounds, for the sun's shadow map to cover. A level without geometry keeps the default
    if (bounds.min.x <= bounds.max.x)
    {
        world.bounds = bounds;
        world.lighting.bounds = bounds;
    }

    // Collision and the lightmap layout, only for full builds
    world.colliders.clear();
    world.bounceBaked = false;
    world.lighting.lightmapTexture = 0;
    world.hasGeometryBuilt = full;
    if (!full) return;
    for (const GeoMesh &mesh : geometry.meshes)
    {
        if (mesh.shape < 0 || mesh.shape >= (int)geometry.shapes.size()) continue;
        Matrix transform = GeoMeshTransform(mesh);
        for (std::vector<std::vector<Vector3>> &piece : GeoConvexPieces(geometry.shapes[mesh.shape]))
        {
            for (std::vector<Vector3> &face : piece) for (Vector3 &p : face) p = Vector3Transform(p, transform);
            world.colliders.push_back(MakeConvexBrush(piece));
        }
    }
    BuildLightmapLayout(world);

    // Laid out the same as before (the geometry didn't change): the lightmap there is still fits
    if (world.lightmapKey != 0 && world.lightmap.id != 0)
    {
        world.bounceBaked = true;
        world.lighting.lightmapTexture = world.lightmap.id;
    }
    TraceLog(LOG_INFO, "WORLD: Built %d meshes into %d triangles, %d collision pieces", (int)geometry.meshes.size(),
             model.meshCount > 0 ? (int)world.bounceTriangles.size() : 0, (int)world.colliders.size());
}

static void UnloadWorldModel(Model &model, std::vector<WorldMaterial> &materials);

void UnloadWorld(World &world)
{
    if (world.model.meshCount > 0 || world.model.materialCount > 0) UnloadModel(world.model);
    world.model = { 0 };
    for (auto &[path, texture] : world.textureCache)
    {
        if (texture.id != rlGetTextureIdDefault()) UnloadTexture(texture);
    }
    world.textureCache.clear();
    for (auto &[path, material] : world.materialCache) UnloadWorldMaterial(material);
    world.materialCache.clear();
    world.materials.clear();
    for (PropModel &propModel : world.propModels)
    {
        if (propModel.model.meshCount > 0) UnloadWorldModel(propModel.model, propModel.materials);
    }
    world.propModels.clear();
    ClearProps(world);
    ClearCubemaps(world);
    UnloadLighting(world.lighting);
    UnloadAmbientOcclusion(world.ao);
    UnloadSkybox(world.skybox);
    UnloadColorGrade(world.colorGrade);
    world.colliders.clear();
    world.movers.clear();
    CancelLightmapBake(world);
    if (world.lightmap.id != 0) UnloadTexture(world.lightmap);
    world.lightmap = { 0 };
    world.lighting.lightmapTexture = 0;
    world.bounceTriangles.clear();
    world.lightmapTexels.clear();
    world.lightmapKey = 0;
    world.bounceBaked = false;
    world.hasGeometryBuilt = false;
}

void RenderWorldShadows(World &world)
{
    std::vector<ShadowCaster> casters = { { &world.model, world.model.transform } };
    if (!world.propsHidden) for (const WorldProp &prop : world.props) casters.push_back({ &world.propModels[prop.model].model, prop.transform });
    RenderShadowMap(world.lighting, casters);
}

void RenderWorldAO(World &world, Camera camera, int width, int height, bool enabled, float radius)
{
    if (!enabled)
    {
        world.lighting.aoTexture = 0;
        return;
    }

    // Only the world mesh goes in, so entities never darken the level around them
    RenderAmbientOcclusion(world.ao, world.model, camera, width, height, radius);
    world.lighting.aoTexture = world.ao.blurred.texture.id;
    world.lighting.aoScreenSize = { (float)width, (float)height };
}

// A material map, through the game data. Warns and gives an empty texture if it can't be loaded
static Texture LoadMaterialMap(const std::string &path, const std::string &diffusePath, const char *kind)
{
    Texture texture = { 0 };
    if (path.empty()) return texture;
    if (!LoadGameTexture(path.c_str(), texture))
    {
        TraceLog(LOG_WARNING, "MATERIALS: [%s] Couldn't load its %s %s", diffusePath.c_str(), kind, path.c_str());
        texture = { 0 };
    }
    return texture;
}

static void ApplyMaterialList(std::vector<WorldMaterial> &materials, const MaterialLibrary &library)
{
    for (WorldMaterial &material : materials)
    {
        for (Texture *map : { &material.bumpmap, &material.specularmap, &material.glossmap })
        {
            if (map->id != 0) UnloadTexture(*map);
            *map = { 0 };
        }
        material.def = FindMaterial(library, material.diffusePath);
        material.bumpmap = LoadMaterialMap(material.def.bumpmap, material.diffusePath, "bump map");
        material.specularmap = LoadMaterialMap(material.def.specularmap, material.diffusePath, "specular map");
        material.glossmap = LoadMaterialMap(material.def.glossmap, material.diffusePath, "gloss map");
    }
}

void ApplyMaterials(World &world, const MaterialLibrary &library)
{
    // The world's materials live in its cache; the model's list is copies of them
    std::vector<WorldMaterial> cached;
    for (auto &[path, material] : world.materialCache) cached.push_back(material);
    ApplyMaterialList(cached, library);
    for (const WorldMaterial &material : cached) world.materialCache[material.diffusePath] = material;
    for (WorldMaterial &material : world.materials) material = world.materialCache[material.diffusePath];

    for (PropModel &propModel : world.propModels) ApplyMaterialList(propModel.materials, library);
}

// Unloads a model loaded by LoadWorldModel: its own textures, its maps, then the model
static void UnloadWorldModel(Model &model, std::vector<WorldMaterial> &materials)
{
    // UnloadModel leaves shaders and textures alone, since they can be shared between models.
    // These textures are the model's own, so unload them first, or every reload would leak them
    for (int i = 0; i < model.materialCount; i++)
    {
        Texture texture = model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
        if (texture.id != rlGetTextureIdDefault()) UnloadTexture(texture);
    }
    ApplyMaterialList(materials, MaterialLibrary());  // Unloads their maps
    materials.clear();
    UnloadModel(model);
}

// ---------------------------------------------------------------------------------------------
// Props

int LoadPropModel(World &world, const std::string &modelPath)
{
    for (size_t i = 0; i < world.propModels.size(); i++)
    {
        if (world.propModels[i].path == modelPath) return (world.propModels[i].model.meshCount > 0) ? (int)i : -1;
    }

    // Remembered even if it fails, so a bad path isn't tried again for every prop that uses it
    PropModel propModel = {};
    propModel.path = modelPath;
    if (GameDataFileExists(modelPath.c_str()))
    {
        propModel.model = LoadWorldModel(modelPath.c_str(), propModel.materials);
        for (int i = 0; i < propModel.model.materialCount; i++) propModel.model.materials[i].shader = world.lighting.shader;
        propModel.bounds = GetModelBoundingBox(propModel.model);
        ApplyMaterialList(propModel.materials, LoadMaterialLibrary());
    }
    else
    {
        TraceLog(LOG_WARNING, "WORLD: [%s] Prop model not found", modelPath.c_str());
    }
    world.propModels.push_back(propModel);
    return (propModel.model.meshCount > 0) ? (int)world.propModels.size() - 1 : -1;
}

void AddProp(World &world, int model, Vector3 origin, float angle, float scale, bool isStatic, bool solid)
{
    if (model < 0 || model >= (int)world.propModels.size()) return;

    WorldProp prop;
    prop.model = model;
    prop.transform = MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixRotateY(angle*DEG2RAD)),
                                    MatrixTranslate(origin.x, origin.y, origin.z));

    // The box around its 8 turned corners
    BoundingBox local = world.propModels[model].bounds;
    prop.bounds = { { FLT_MAX, FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX, -FLT_MAX } };
    for (int corner = 0; corner < 8; corner++)
    {
        Vector3 p = { (corner & 1) ? local.max.x : local.min.x, (corner & 2) ? local.max.y : local.min.y, (corner & 4) ? local.max.z : local.min.z };
        p = Vector3Transform(p, prop.transform);
        prop.bounds.min = Vector3Min(prop.bounds.min, p);
        prop.bounds.max = Vector3Max(prop.bounds.max, p);
    }

    prop.isStatic = isStatic;
    world.props.push_back(prop);
    if (isStatic && solid) world.propColliders.push_back(MakeBoxBrush(prop.bounds));
}

void ClearProps(World &world)
{
    for (WorldProp &prop : world.props)
    {
        for (Mesh &mesh : prop.lightmapMeshes) UnloadMesh(mesh);
    }
    world.props.clear();
    world.propColliders.clear();
}

// ---------------------------------------------------------------------------------------------
// Materials outside the world model and props

WorldMaterial LoadWorldMaterial(const std::string &diffusePath, const MaterialLibrary &library)
{
    std::vector<WorldMaterial> materials(1);
    materials[0].diffusePath = diffusePath;
    ApplyMaterialList(materials, library);
    return materials[0];
}

void UnloadWorldMaterial(WorldMaterial &material)
{
    std::vector<WorldMaterial> materials = { material };
    ApplyMaterialList(materials, MaterialLibrary());  // Unloads its maps
    material = WorldMaterial();
}

void SetLightingWorldMaterial(const Lighting &lighting, const WorldMaterial &material)
{
    SetLightingMaterial(lighting, material.bumpmap.id, material.specularmap.id, material.glossmap.id,
                        material.def.bumpmapOpenGL, material.def.specular, material.def.gloss, material.def.reflection);
}

// ---------------------------------------------------------------------------------------------
// Cubemaps

void AddCubemap(World &world, Vector3 position, int size)
{
    WorldCubemap cubemap;
    cubemap.position = position;
    cubemap.size = size;
    cubemap.texture = rlLoadTextureCubemap(nullptr, size, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    rlTextureParameters(cubemap.texture, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(cubemap.texture, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
    world.cubemaps.push_back(cubemap);
    world.lighting.cubemapPositions.push_back(position);
    world.lighting.cubemapTextures.push_back(cubemap.texture);
    world.cubemapsStale = true;
}

void ClearCubemaps(World &world)
{
    for (const WorldCubemap &cubemap : world.cubemaps) rlUnloadTexture(cubemap.texture);
    world.cubemaps.clear();
    world.lighting.cubemapPositions.clear();
    world.lighting.cubemapTextures.clear();
}

void CaptureCubemaps(World &world)
{
    if (world.cubemaps.empty()) return;

    // Lit the way it is right now. Without reflections, so cubemaps don't show each other, and without the
    // screen's ambient occlusion, which only fits the player's view
    RenderWorldShadows(world);
    world.lighting.capturingCubemaps = true;
    unsigned int aoTexture = world.lighting.aoTexture;
    world.lighting.aoTexture = 0;

    // The six faces, the way OpenGL lays out a cubemap: +X, -X, +Y, -Y, +Z, -Z
    const Vector3 directions[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const Vector3 ups[6] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };

    rlDrawRenderBatchActive();
    for (const WorldCubemap &cubemap : world.cubemaps)
    {
        unsigned int framebuffer = rlLoadFramebuffer();
        unsigned int depth = rlLoadTextureDepth(cubemap.size, cubemap.size, true);
        rlFramebufferAttach(framebuffer, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);

        for (int face = 0; face < 6; face++)
        {
            rlFramebufferAttach(framebuffer, cubemap.texture, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X + face, 0);
            rlEnableFramebuffer(framebuffer);
            rlViewport(0, 0, cubemap.size, cubemap.size);
            rlClearColor(245, 245, 245, 255);  // RAYWHITE, like the game's background where there's no sky
            rlClearScreenBuffers();

            Camera camera = { cubemap.position, Vector3Add(cubemap.position, directions[face]), ups[face], 90.0f, CAMERA_PERSPECTIVE };
            rlSetMatrixProjection(MatrixPerspective(90.0*DEG2RAD, 1.0, 0.05, 1000.0));
            rlSetMatrixModelview(MatrixLookAt(camera.position, camera.target, camera.up));
            rlEnableDepthTest();
            DrawSkybox(world.skybox, camera);
            DrawWorld(world, false);
            rlDrawRenderBatchActive();
        }
        rlDisableFramebuffer();
        rlUnloadFramebuffer(framebuffer);  // Also unloads the depth buffer attached to it
    }

    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
    world.lighting.capturingCubemaps = false;
    world.lighting.aoTexture = aoTexture;
    world.cubemapsStale = false;

    // Back to drawing on the screen in 2D, the way EndTextureMode leaves it, so this can run mid-frame
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, GetRenderWidth(), GetRenderHeight(), 0, 0.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    TraceLog(LOG_INFO, "WORLD: Captured %d cubemaps", (int)world.cubemaps.size());
}

void DrawWorld(const World &world, bool showColliders)
{
    BeginLighting(world.lighting);
        // Only the world mesh gets ambient occlusion and baked bounce
        SetLightingAO(world.lighting, true);
        SetLightingBounce(world.lighting, world.bounceBaked);

        // A mesh per material, each with its maps
        const Model &model = world.model;
        for (int i = 0; i < model.meshCount; i++)
        {
            int index = model.meshMaterial[i];
            if (index < (int)world.materials.size())
            {
                SetLightingWorldMaterial(world.lighting, world.materials[index]);
            }
            DrawMesh(model.meshes[i], model.materials[index], model.transform);
        }
        ClearLightingMaterial(world.lighting);

        SetLightingAO(world.lighting, false);
        SetLightingBounce(world.lighting, false);

        // Props, with their own materials. Static ones have their part of the bounce lightmap once the layout
        // has them. No ambient occlusion, which is worked out for the world mesh only
        for (const WorldProp &prop : world.props)
        {
            if (world.propsHidden) break;
            const PropModel &propModel = world.propModels[prop.model];
            bool baked = !prop.lightmapMeshes.empty();
            SetLightingBounce(world.lighting, baked && world.bounceBaked);
            for (int i = 0; i < propModel.model.meshCount; i++)
            {
                int index = propModel.model.meshMaterial[i];
                if (index < (int)propModel.materials.size())
                {
                    SetLightingWorldMaterial(world.lighting, propModel.materials[index]);
                }
                DrawMesh(baked ? prop.lightmapMeshes[i] : propModel.model.meshes[i], propModel.model.materials[index], prop.transform);
            }
        }
        ClearLightingMaterial(world.lighting);
        SetLightingBounce(world.lighting, false);
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
        for (const Brush &brush : world.propColliders)
        {
            for (const std::vector<Vector3> &face : brush.faces)
            {
                for (size_t i = 0; i < face.size(); i++) DrawLine3D(face[i], face[(i + 1)%face.size()], SKYBLUE);
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

Brush MakeConvexBrush(const std::vector<std::vector<Vector3>> &faces)
{
    return BuildBrush(faces, -1);
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
    for (const Brush &brush : world.movers) ClipBoxToBrush(trace, brush, start, end, mins, maxs);
    for (const Brush &brush : world.propColliders) ClipBoxToBrush(trace, brush, start, end, mins, maxs);

    trace.endPos = Vector3Lerp(start, end, trace.fraction);
    return trace;
}

Brush MakeBoxBrush(BoundingBox box)
{
    Vector3 a = box.min, b = box.max;
    const std::vector<std::vector<Vector3>> faces = {
        { { a.x, a.y, a.z }, { a.x, a.y, b.z }, { a.x, b.y, b.z }, { a.x, b.y, a.z } },  // -X
        { { b.x, a.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { b.x, a.y, b.z } },  // +X
        { { a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, a.y, b.z }, { a.x, a.y, b.z } },  // -Y
        { { a.x, b.y, a.z }, { a.x, b.y, b.z }, { b.x, b.y, b.z }, { b.x, b.y, a.z } },  // +Y
        { { a.x, a.y, a.z }, { a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, a.y, a.z } },  // -Z
        { { a.x, a.y, b.z }, { b.x, a.y, b.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z } },  // +Z
    };
    return BuildBrush(faces, -1);
}

bool LoadGameTexture(const char *path, Texture &texture)
{
    ModelMaterial material;
    material.texturePath = path;
    return LoadWorldTexture(material, GetDirectoryPath(path), texture);
}

bool GetGameTextureSize(const char *path, int &width, int &height)
{
    int fileSize = 0;
    unsigned char *fileData = LoadFileData(path, &fileSize);  // Through the game data
    if (fileData == nullptr) return false;
    int channels;
    bool found = stbi_info_from_memory(fileData, fileSize, &width, &height, &channels) != 0;
    UnloadFileData(fileData);
    return found;
}
