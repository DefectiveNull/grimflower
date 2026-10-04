#include "lighting.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>

static const int SHADOW_MAP_SIZE = 2048;
static const int SHADOW_MAP_SLOT = 10;  // Texture slot the shadow map is bound to, clear of the material maps raylib binds

static const int POINT_SHADOW_TILE_SIZE = 512;  // Resolution of each cube face
static const int POINT_SHADOW_ATLAS_SLOT = 11;
static const float POINT_SHADOW_NEAR = 0.05f;

// Per-pixel lighting. The vertex shader only passes the world position and normal along;
// all the lighting happens per pixel in the fragment shader
static const char *LIGHTING_VS = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

static const char *LIGHTING_FS = R"(
#version 330
#define MAX_LIGHTS 16
#define MAX_SHADOW_LIGHTS 4

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambient;
uniform mat4 sunViewProjection;
uniform sampler2D shadowMap;
uniform int shadowMapSize;
uniform float shadowDepthScale;

uniform int lightCount;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform float lightRadii[MAX_LIGHTS];

uniform sampler2D pointShadowAtlas;
uniform mat4 pointShadowMatrices[MAX_SHADOW_LIGHTS*6];
uniform int pointShadowSlots[MAX_LIGHTS];
uniform int pointShadowTileSize;

out vec4 finalColor;

// 0 = fully lit by the sun, 1 = fully in shadow
float SunShadow(vec3 normal, float facing)
{
    vec4 lightSpace = sunViewProjection*vec4(fragPosition, 1.0);
    vec3 coords = lightSpace.xyz/lightSpace.w*0.5 + 0.5;
    if (coords.x < 0.0 || coords.x > 1.0 || coords.y < 0.0 || coords.y > 1.0 || coords.z > 1.0) return 0.0;

    // Surfaces at a grazing angle to the sun need more bias to avoid shadow acne. In world units, then to depth
    float bias = mix(0.05, 0.01, facing)*shadowDepthScale;

    // Percentage-closer filtering: average a 3x3 block of shadow map texels for soft edges
    float texel = 1.0/float(shadowMapSize);
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float closest = texture(shadowMap, coords.xy + vec2(x, y)*texel).r;
            shadow += (coords.z - bias > closest) ? 1.0 : 0.0;
        }
    }
    return shadow/9.0;
}

// 0 = fully lit by point light i, 1 = fully in shadow. Its 6 cube faces are a row of tiles in the atlas
float PointShadow(int i, int slot, vec3 fromLight, float facing)
{
    // The cube face whose 90 degree view contains this pixel: +X, -X, +Y, -Y, +Z, -Z
    vec3 a = abs(fromLight);
    int face;
    if (a.x >= a.y && a.x >= a.z) face = (fromLight.x > 0.0) ? 0 : 1;
    else if (a.y >= a.z) face = (fromLight.y > 0.0) ? 2 : 3;
    else face = (fromLight.z > 0.0) ? 4 : 5;

    // Where this pixel lands on that face, kept a texel and a half from the tile's edges so the filtering
    // below doesn't read the neighbouring tile
    vec4 clip = pointShadowMatrices[slot*6 + face]*vec4(fragPosition, 1.0);
    float texel = 1.0/float(pointShadowTileSize);
    vec2 uv = clamp(clip.xy/clip.w*0.5 + 0.5, vec2(1.5*texel), vec2(1.0 - 1.5*texel));

    vec2 tiles = vec2(6.0, float(MAX_SHADOW_LIGHTS));
    vec2 atlasUV = (vec2(float(face), float(slot)) + uv)/tiles;
    vec2 atlasTexel = vec2(texel)/tiles;

    // Stored values are distance/radius, so the bias is too
    float current = length(fromLight)/lightRadii[i];
    float bias = mix(0.05, 0.01, facing)/lightRadii[i];

    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float closest = texture(pointShadowAtlas, atlasUV + vec2(x, y)*atlasTexel).r;
            shadow += (current - bias > closest) ? 1.0 : 0.0;
        }
    }
    return shadow/9.0;
}

void main()
{
    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
    vec3 normal = normalize(fragNormal);

    vec3 light = ambient;

    float sunFacing = max(dot(normal, -sunDirection), 0.0);
    if (sunFacing > 0.0) light += sunColor*sunFacing*(1.0 - SunShadow(normal, sunFacing));

    for (int i = 0; i < lightCount; i++)
    {
        vec3 toLight = lightPositions[i] - fragPosition;
        float distance = length(toLight);
        float falloff = clamp(1.0 - distance/lightRadii[i], 0.0, 1.0);
        float facing = max(dot(normal, toLight/max(distance, 0.0001)), 0.0);
        if (facing <= 0.0 || falloff <= 0.0) continue;

        float shadow = (pointShadowSlots[i] >= 0) ? PointShadow(i, pointShadowSlots[i], -toLight, facing) : 0.0;
        light += lightColors[i]*facing*falloff*falloff*(1.0 - shadow);
    }

    finalColor = vec4(texel.rgb*light, texel.a);
}
)";

// Only writes depth, for the shadow map
static const char *DEPTH_VS = R"(
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main()
{
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

static const char *DEPTH_FS = R"(
#version 330
out vec4 finalColor;
void main()
{
    finalColor = vec4(1.0);
}
)";

// Point light shadow depth: stores how far each surface is from the light, as a fraction of its radius,
// instead of the usual perspective depth, so the lighting shader can compare distances directly
static const char *POINT_DEPTH_VS = R"(
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fragPosition;
void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

static const char *POINT_DEPTH_FS = R"(
#version 330
in vec3 fragPosition;
uniform vec3 lightPosition;
uniform float lightRadius;
out vec4 finalColor;
void main()
{
    gl_FragDepth = length(fragPosition - lightPosition)/lightRadius;
    finalColor = vec4(1.0);
}
)";

// A render target with only a depth texture, no color
static RenderTexture2D LoadShadowMapTexture(int width, int height)
{
    RenderTexture2D target = { 0 };
    target.id = rlLoadFramebuffer();
    target.texture.width = width;
    target.texture.height = height;

    rlEnableFramebuffer(target.id);
    target.depth.id = rlLoadTextureDepth(width, height, false);
    target.depth.width = width;
    target.depth.height = height;
    target.depth.mipmaps = 1;
    rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(target.id)) TraceLog(LOG_WARNING, "LIGHTING: Shadow map framebuffer is incomplete");
    rlDisableFramebuffer();

    return target;
}

static Vector3 ColorToVector(Color color, float intensity)
{
    return Vector3Scale({ color.r/255.0f, color.g/255.0f, color.b/255.0f }, intensity);
}

void InitLighting(Lighting &lighting, BoundingBox bounds)
{
    lighting.bounds = bounds;
    lighting.lights.clear();

    // Sun above one corner of the area, shining toward its center, so the light comes in at an angle
    Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
    lighting.sun.position = { bounds.min.x, bounds.max.y + 10.0f, bounds.min.z };
    lighting.sun.direction = Vector3Normalize(Vector3Subtract(center, lighting.sun.position));
    lighting.sun.color = { 255, 244, 214, 255 };  // Slightly warm white
    lighting.sun.intensity = 1.0f;
    lighting.sun.ambient = 0.35f;

    Shader &shader = lighting.shader;
    shader = LoadShaderFromMemory(LIGHTING_VS, LIGHTING_FS);
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader, "matNormal");
    lighting.sunDirectionLoc = GetShaderLocation(shader, "sunDirection");
    lighting.sunColorLoc = GetShaderLocation(shader, "sunColor");
    lighting.ambientLoc = GetShaderLocation(shader, "ambient");
    lighting.sunViewProjectionLoc = GetShaderLocation(shader, "sunViewProjection");
    lighting.shadowMapLoc = GetShaderLocation(shader, "shadowMap");
    lighting.shadowMapSizeLoc = GetShaderLocation(shader, "shadowMapSize");
    lighting.shadowDepthScaleLoc = GetShaderLocation(shader, "shadowDepthScale");
    lighting.lightCountLoc = GetShaderLocation(shader, "lightCount");
    lighting.lightPositionsLoc = GetShaderLocation(shader, "lightPositions");
    lighting.lightColorsLoc = GetShaderLocation(shader, "lightColors");
    lighting.lightRadiiLoc = GetShaderLocation(shader, "lightRadii");

    lighting.pointShadowAtlasLoc = GetShaderLocation(shader, "pointShadowAtlas");
    lighting.pointShadowMatricesLoc = GetShaderLocation(shader, "pointShadowMatrices");
    lighting.pointShadowSlotsLoc = GetShaderLocation(shader, "pointShadowSlots");
    lighting.pointShadowTileSizeLoc = GetShaderLocation(shader, "pointShadowTileSize");

    lighting.depthShader = LoadShaderFromMemory(DEPTH_VS, DEPTH_FS);
    lighting.depthMaterial = LoadMaterialDefault();
    lighting.depthMaterial.shader = lighting.depthShader;

    lighting.pointDepthShader = LoadShaderFromMemory(POINT_DEPTH_VS, POINT_DEPTH_FS);
    lighting.pointDepthShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(lighting.pointDepthShader, "matModel");
    lighting.pointDepthLightPositionLoc = GetShaderLocation(lighting.pointDepthShader, "lightPosition");
    lighting.pointDepthLightRadiusLoc = GetShaderLocation(lighting.pointDepthShader, "lightRadius");
    lighting.pointDepthMaterial = LoadMaterialDefault();
    lighting.pointDepthMaterial.shader = lighting.pointDepthShader;

    lighting.shadowMap = LoadShadowMapTexture(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    lighting.pointShadowAtlas = LoadShadowMapTexture(6*POINT_SHADOW_TILE_SIZE, MAX_SHADOW_LIGHTS*POINT_SHADOW_TILE_SIZE);
    lighting.sunViewProjection = MatrixIdentity();
    lighting.shadowDepthScale = 1.0f;
    for (Matrix &m : lighting.pointShadowMatrices) m = MatrixIdentity();
    for (int &slot : lighting.pointShadowSlots) slot = -1;
}

void UnloadLighting(Lighting &lighting)
{
    UnloadShader(lighting.shader);
    UnloadMaterial(lighting.depthMaterial);       // Also unloads depthShader
    UnloadMaterial(lighting.pointDepthMaterial);  // Also unloads pointDepthShader
    rlUnloadTexture(lighting.shadowMap.depth.id);
    rlUnloadFramebuffer(lighting.shadowMap.id);
    rlUnloadTexture(lighting.pointShadowAtlas.depth.id);
    rlUnloadFramebuffer(lighting.pointShadowAtlas.id);
    lighting.lights.clear();
}

// Renders each shadowed point light's 6 cube faces into its row of the atlas
static void RenderPointShadows(Lighting &lighting, const Model &model)
{
    for (int &slot : lighting.pointShadowSlots) slot = -1;

    // Looking down +X, -X, +Y, -Y, +Z, -Z, in the order the shader picks faces in
    const Vector3 directions[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const Vector3 ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };

    BeginTextureMode(lighting.pointShadowAtlas);
        ClearBackground(WHITE);  // Depth 1 everywhere, meaning nothing within the light's radius
        rlEnableDepthTest();
        rlSetCullFace(RL_CULL_FACE_FRONT);  // Back faces only, to avoid shadow acne, same as the sun

        int shadowCount = 0;
        for (size_t i = 0; i < lighting.lights.size() && i < MAX_POINT_LIGHTS && shadowCount < MAX_SHADOW_LIGHTS; i++)
        {
            const PointLight &light = lighting.lights[i];
            float radius = fmaxf(light.radius, POINT_SHADOW_NEAR*2.0f);
            int slot = shadowCount++;
            lighting.pointShadowSlots[i] = slot;

            SetShaderValue(lighting.pointDepthShader, lighting.pointDepthLightPositionLoc, &light.position, SHADER_UNIFORM_VEC3);
            SetShaderValue(lighting.pointDepthShader, lighting.pointDepthLightRadiusLoc, &radius, SHADER_UNIFORM_FLOAT);

            Matrix projection = MatrixPerspective(90.0*DEG2RAD, 1.0, POINT_SHADOW_NEAR, radius);
            for (int face = 0; face < 6; face++)
            {
                Matrix view = MatrixLookAt(light.position, Vector3Add(light.position, directions[face]), ups[face]);
                lighting.pointShadowMatrices[slot*6 + face] = MatrixMultiply(view, projection);

                rlViewport(face*POINT_SHADOW_TILE_SIZE, slot*POINT_SHADOW_TILE_SIZE, POINT_SHADOW_TILE_SIZE, POINT_SHADOW_TILE_SIZE);
                rlSetMatrixProjection(projection);
                rlSetMatrixModelview(view);
                for (int m = 0; m < model.meshCount; m++) DrawMesh(model.meshes[m], lighting.pointDepthMaterial, model.transform);
            }
        }

        rlSetCullFace(RL_CULL_FACE_BACK);
        rlDisableDepthTest();
    EndTextureMode();
}

void RenderShadowMap(Lighting &lighting, const Model &model)
{
    Vector3 direction = Vector3Normalize(lighting.sun.direction);
    Vector3 center = Vector3Scale(Vector3Add(lighting.bounds.min, lighting.bounds.max), 0.5f);
    float radius = Vector3Length(Vector3Subtract(lighting.bounds.max, lighting.bounds.min))*0.5f;

    // Orthographic camera looking along the sun's direction, framing the whole area. Its clip planes are fitted
    // tightly around the area, so the shadow map's depth precision isn't spread over empty space
    Camera camera = { 0 };
    camera.position = Vector3Subtract(center, Vector3Scale(direction, radius + 1.0f));
    camera.target = center;
    camera.up = (fabsf(direction.y) > 0.99f) ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = radius*2.0f;
    camera.projection = CAMERA_ORTHOGRAPHIC;

    const double nearPlane = 0.1;
    const double farPlane = 2.0*radius + 2.0;
    lighting.shadowDepthScale = (float)(1.0/(farPlane - nearPlane));

    double oldNear = rlGetCullDistanceNear();
    double oldFar = rlGetCullDistanceFar();

    BeginTextureMode(lighting.shadowMap);
        ClearBackground(WHITE);
        rlSetClipPlanes(nearPlane, farPlane);
        BeginMode3D(camera);
            lighting.sunViewProjection = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());

            // Draw back faces only: the depth stored is then the far side of each surface, so surfaces
            // facing the sun don't shadow themselves (shadow acne)
            rlSetCullFace(RL_CULL_FACE_FRONT);
            for (int i = 0; i < model.meshCount; i++) DrawMesh(model.meshes[i], lighting.depthMaterial, model.transform);
            rlSetCullFace(RL_CULL_FACE_BACK);
        EndMode3D();
    EndTextureMode();

    rlSetClipPlanes(oldNear, oldFar);

    RenderPointShadows(lighting, model);
}

void BeginLighting(const Lighting &lighting)
{
    const Shader &shader = lighting.shader;
    const Sun &sun = lighting.sun;

    Vector3 sunDirection = Vector3Normalize(sun.direction);
    Vector3 sunColor = ColorToVector(sun.color, sun.intensity);
    Vector3 ambient = { sun.ambient, sun.ambient, sun.ambient };
    SetShaderValue(shader, lighting.sunDirectionLoc, &sunDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lighting.sunColorLoc, &sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lighting.ambientLoc, &ambient, SHADER_UNIFORM_VEC3);

    Vector3 positions[MAX_POINT_LIGHTS];
    Vector3 colors[MAX_POINT_LIGHTS];
    float radii[MAX_POINT_LIGHTS];
    int count = 0;
    for (const PointLight &light : lighting.lights)
    {
        if (count == MAX_POINT_LIGHTS) break;
        positions[count] = light.position;
        colors[count] = ColorToVector(light.color, light.intensity);
        radii[count] = fmaxf(light.radius, 0.001f);
        count++;
    }
    SetShaderValue(shader, lighting.lightCountLoc, &count, SHADER_UNIFORM_INT);
    if (count > 0)
    {
        SetShaderValueV(shader, lighting.lightPositionsLoc, positions, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(shader, lighting.lightColorsLoc, colors, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(shader, lighting.lightRadiiLoc, radii, SHADER_UNIFORM_FLOAT, count);
    }

    // Shadow map
    int slot = SHADOW_MAP_SLOT;
    int shadowMapSize = SHADOW_MAP_SIZE;
    SetShaderValueMatrix(shader, lighting.sunViewProjectionLoc, lighting.sunViewProjection);
    SetShaderValue(shader, lighting.shadowMapLoc, &slot, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.shadowMapSizeLoc, &shadowMapSize, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.shadowDepthScaleLoc, &lighting.shadowDepthScale, SHADER_UNIFORM_FLOAT);

    // Point light shadows
    int atlasSlot = POINT_SHADOW_ATLAS_SLOT;
    int tileSize = POINT_SHADOW_TILE_SIZE;
    SetShaderValue(shader, lighting.pointShadowAtlasLoc, &atlasSlot, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.pointShadowTileSizeLoc, &tileSize, SHADER_UNIFORM_INT);
    SetShaderValueV(shader, lighting.pointShadowSlotsLoc, lighting.pointShadowSlots, SHADER_UNIFORM_INT, MAX_POINT_LIGHTS);
    rlEnableShader(shader.id);
    rlSetUniformMatrices(lighting.pointShadowMatricesLoc, lighting.pointShadowMatrices, MAX_SHADOW_LIGHTS*6);
    rlDisableShader();

    rlActiveTextureSlot(SHADOW_MAP_SLOT);
    rlEnableTexture(lighting.shadowMap.depth.id);
    rlActiveTextureSlot(POINT_SHADOW_ATLAS_SLOT);
    rlEnableTexture(lighting.pointShadowAtlas.depth.id);
    rlActiveTextureSlot(0);
}

void EndLighting()
{
    rlActiveTextureSlot(SHADOW_MAP_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(POINT_SHADOW_ATLAS_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(0);
}
