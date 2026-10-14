#include "lighting.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <string>

// Two OpenGL 1.1 functions rlgl has no wrapper for, straight from opengl32 (windows.h would clash with raylib)
extern "C" __declspec(dllimport) void __stdcall glBindTexture(unsigned int target, unsigned int texture);
extern "C" __declspec(dllimport) void __stdcall glTexParameteri(unsigned int target, unsigned int name, int value);
static const unsigned int GL_TEXTURE_2D_ = 0x0DE1;
static const unsigned int GL_TEXTURE_MAG_FILTER_ = 0x2800, GL_TEXTURE_MIN_FILTER_ = 0x2801, GL_LINEAR_ = 0x2601;
static const unsigned int GL_TEXTURE_COMPARE_MODE_ = 0x884C, GL_TEXTURE_COMPARE_FUNC_ = 0x884D;
static const unsigned int GL_COMPARE_REF_TO_TEXTURE_ = 0x884E, GL_LEQUAL_ = 0x0203;

static const int SHADOW_MAP_SIZE = 4096;
static const int SHADOW_MAP_SLOT = 10;  // Texture slot the shadow map is bound to, clear of the material maps raylib binds

static const int POINT_SHADOW_TILE_SIZE = 512;  // Resolution of each cube face
static const int POINT_SHADOW_ATLAS_SLOT = 11;
static const int AO_SLOT = 12;
static const int LIGHTMAP_SLOT = 13;
static const int BUMP_MAP_SLOT = 7;      // A material's maps. Under 16, the fewest texture units a GPU can have
static const int SPECULAR_MAP_SLOT = 8;
static const int GLOSS_MAP_SLOT = 9;
static const int FIRST_CUBEMAP_SLOT = 1;  // Cubemaps on 1 to 4: raylib only binds slot 0, the diffuse texture
static const float POINT_SHADOW_NEAR = 1.6f;

// Per-pixel lighting. The vertex shader only passes the world position and normal along;
// all the lighting happens per pixel in the fragment shader
static const char *LIGHTING_VS = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;  // Where it is in the lightmap
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec4 fragColor;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    fragTexCoord2 = vertexTexCoord2;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

// InitLighting puts "#version 330" and the MAX_LIGHTS and MAX_SHADOW_LIGHTS defines in front, from lighting.h
static const char *LIGHTING_FS = R"(
in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambient;
uniform mat4 sunViewProjection;
uniform sampler2DShadow shadowMap;  // Compares depths as it samples, with filtering, see LoadShadowMapTexture
uniform int shadowMapSize;
uniform float shadowDepthScale;

uniform int lightCount;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform float lightRadii[MAX_LIGHTS];

uniform sampler2DShadow pointShadowAtlas;
uniform int pointShadowSlots[MAX_LIGHTS];
uniform int pointShadowTileSize;
uniform int pointShadowRows;  // Rows the atlas has, one per shadowed light

// Each cube face's view, the same as RenderPointShadows renders them with: looking down +X, -X, +Y, -Y, +Z, -Z
const vec3 FACE_DIRECTIONS[6] = vec3[](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 FACE_UPS[6] = vec3[](vec3(0, 1, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 0));

uniform sampler2D aoMap;    // 1 = open, 0 = hemmed in, per screen pixel
uniform int aoEnabled;
uniform vec2 aoScreenSize;
uniform float aoDirect;     // How much the AO darkens direct light too, 0 to 1

// The world mesh's baked sun bounce, in a lightmap as bounce/2 (bounce.cpp), read at fragTexCoord2. Only
// while bounceEnabled is on, which is only for the world mesh
uniform sampler2D lightmap;
uniform int bounceEnabled;
uniform float bounceScale;

// The mesh's material, besides its diffuse texture (materials.h). SetLightingMaterial sets these per mesh;
// everything else gets no bump map and no shine
uniform sampler2D bumpMap;
uniform sampler2D specularMap;
uniform sampler2D glossMap;
uniform int hasBumpMap;
uniform int bumpGreenUp;       // OpenGL style bump map: its green points up the texture instead of down
uniform int hasSpecularMap;
uniform int hasGlossMap;
uniform float specularStrength;
uniform float glossiness;
uniform vec3 viewPosition;     // The camera, for highlights
uniform int unlit;             // 1: just the textures, no lighting at all
uniform float reflection;      // How much of the nearest cubemap the material mirrors, masked by its specular map

// The env_cubemaps nearest the camera (up to 4): what the level looks like from each. Each pixel reflects
// the one nearest to it. Separate samplers, since GLSL 330 can't pick from an array of them per pixel
uniform samplerCube cubemap0;
uniform samplerCube cubemap1;
uniform samplerCube cubemap2;
uniform samplerCube cubemap3;
uniform vec3 cubemapPositions[4];
uniform int cubemapCount;

// The baked bounce (bounce.cpp). The lightmap is four layers stacked top to bottom: the bounce reaching the
// surface head on, then from Half-Life 2's three tilted directions. Without a bump map it's the first. With
// one, the three are blended by how much the texel's bump faces each, squared, like Half-Life 2 does, and
// compared with an even blend (what a flat bump gets): the head on bounce is scaled by that, so a flat bump
// looks the same as none, and bumps facing where the bounce comes from get brighter
vec3 Bounce(vec3 bump)
{
    vec2 uv = vec2(fragTexCoord2.x, fragTexCoord2.y*0.25);
    vec3 headOn = texture(lightmap, uv).rgb;
    if (hasBumpMap == 0) return headOn;

    const vec3 BASIS0 = vec3(0.81649658, 0.0, 0.57735027);
    const vec3 BASIS1 = vec3(-0.40824829, 0.70710678, 0.57735027);
    const vec3 BASIS2 = vec3(-0.40824829, -0.70710678, 0.57735027);
    vec3 weights = max(vec3(dot(bump, BASIS0), dot(bump, BASIS1), dot(bump, BASIS2)), 0.0);
    weights *= weights;
    weights /= max(weights.x + weights.y + weights.z, 0.0001);

    vec3 bounce0 = texture(lightmap, uv + vec2(0.0, 0.25)).rgb;
    vec3 bounce1 = texture(lightmap, uv + vec2(0.0, 0.50)).rgb;
    vec3 bounce2 = texture(lightmap, uv + vec2(0.0, 0.75)).rgb;
    vec3 bumped = bounce0*weights.x + bounce1*weights.y + bounce2*weights.z;
    vec3 even = (bounce0 + bounce1 + bounce2)/3.0;
    return headOn*clamp(bumped/max(even, vec3(0.0001)), 0.0, 3.0);
}

vec3 ReflectNearestCubemap(vec3 direction)
{
    int nearest = 0;
    float nearestDistance = 1e30;
    for (int i = 0; i < cubemapCount; i++)
    {
        vec3 offset = cubemapPositions[i] - fragPosition;
        float distance = dot(offset, offset);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = i;
        }
    }
    if (nearest == 0) return texture(cubemap0, direction).rgb;
    if (nearest == 1) return texture(cubemap1, direction).rgb;
    if (nearest == 2) return texture(cubemap2, direction).rgb;
    return texture(cubemap3, direction).rgb;
}

out vec4 finalColor;

// Which ways the texture's x and y run across this pixel's surface, plus its normal, to turn a bump map's
// normals into world space. Worked out from how the position and texture coordinates change between
// neighbouring pixels, so meshes don't need tangents (Christian Schueler's cotangent frame)
mat3 TextureFrame(vec3 normal, vec3 position, vec2 uv)
{
    vec3 dp1 = dFdx(position);
    vec3 dp2 = dFdy(position);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, normal);
    vec3 dp1perp = cross(normal, dp1);
    vec3 tangent = dp2perp*duv1.x + dp1perp*duv2.x;
    vec3 bitangent = dp2perp*duv1.y + dp1perp*duv2.y;
    float scale = inversesqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-20));
    return mat3(tangent*scale, bitangent*scale, normal);
}

// 0 = fully lit by the sun, 1 = fully in shadow
float SunShadow(vec3 normal, float facing)
{
    vec4 lightSpace = sunViewProjection*vec4(fragPosition, 1.0);
    vec3 coords = lightSpace.xyz/lightSpace.w*0.5 + 0.5;
    if (coords.x < 0.0 || coords.x > 1.0 || coords.y < 0.0 || coords.y > 1.0 || coords.z > 1.0) return 0.0;

    // Surfaces at a grazing angle to the sun need more bias to avoid shadow acne. In world units, then to depth
    float bias = mix(1.6, 0.32, facing)*shadowDepthScale;

    // Percentage-closer filtering: a 4x4 grid of filtered lookups, each already a blend of 2x2 texels,
    // so the edge fades smoothly over about 5 texels
    float texel = 1.0/float(shadowMapSize);
    float lit = 0.0;
    for (float x = -1.5; x <= 1.5; x += 1.0)
    {
        for (float y = -1.5; y <= 1.5; y += 1.0)
        {
            lit += texture(shadowMap, vec3(coords.xy + vec2(x, y)*texel, coords.z - bias));
        }
    }
    return 1.0 - lit/16.0;
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

    // Where this pixel lands on that face: its 90 degree perspective view, worked out the way raylib's
    // MatrixLookAt and MatrixPerspective do. Kept a texel and a half from the tile's edges so the filtering
    // below doesn't read the neighbouring tile
    vec3 back = -FACE_DIRECTIONS[face];
    vec3 right = normalize(cross(FACE_UPS[face], back));
    vec3 up = cross(back, right);
    vec2 projected = vec2(dot(right, fromLight), dot(up, fromLight))/dot(FACE_DIRECTIONS[face], fromLight);
    float texel = 1.0/float(pointShadowTileSize);
    vec2 uv = clamp(projected*0.5 + 0.5, vec2(1.5*texel), vec2(1.0 - 1.5*texel));

    vec2 tiles = vec2(6.0, float(pointShadowRows));
    vec2 atlasUV = (vec2(float(face), float(slot)) + uv)/tiles;
    vec2 atlasTexel = vec2(texel)/tiles;

    // Stored values are distance/radius, so the bias is too
    float current = length(fromLight)/lightRadii[i];
    float bias = mix(1.6, 0.32, facing)/lightRadii[i];

    // Filtered lookups like the sun's, in a 2x2 grid so they stay inside the tile's margin
    float lit = 0.0;
    for (float x = -0.5; x <= 0.5; x += 1.0)
    {
        for (float y = -0.5; y <= 0.5; y += 1.0)
        {
            lit += texture(pointShadowAtlas, vec3(atlasUV + vec2(x, y)*atlasTexel, current - bias));
        }
    }
    return 1.0 - lit/4.0;
}

void main()
{
    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
    if (unlit == 1)
    {
        finalColor = texel;  // Shaded unlit, the editor's view: textures only
        return;
    }

    // The face's own normal decides what's in shadow and what faces away from a light. The bump map's
    // normal, if there is one, decides how much light each texel catches and where its highlights are
    vec3 faceNormal = normalize(fragNormal);
    vec3 normal = faceNormal;
    vec3 bump = vec3(0.0, 0.0, 1.0);  // In the texture's own space: x and y along it, z off the surface
    if (hasBumpMap == 1)
    {
        bump = normalize(texture(bumpMap, fragTexCoord).xyz*2.0 - 1.0);
        if (bumpGreenUp == 1) bump.y = -bump.y;
        normal = normalize(TextureFrame(faceNormal, fragPosition, fragTexCoord)*bump);
    }

    // Specular, Blinn-Phong: the material's shine, times its specular map, with the gloss setting the
    // highlight's size. Scaled up as it gets tighter, so a sharp highlight is as bright as a wide one overall
    vec3 shineMask = (hasSpecularMap == 1) ? texture(specularMap, fragTexCoord).rgb : vec3(1.0);
    vec3 shine = specularStrength*shineMask;
    float gloss = glossiness;
    if (hasGlossMap == 1) gloss *= texture(glossMap, fragTexCoord).r;
    float exponent = exp2(1.0 + gloss*10.0);  // 2 to 2048
    float highlightScale = (exponent + 8.0)/8.0;
    bool shiny = specularStrength > 0.0;
    vec3 toEye = normalize(viewPosition - fragPosition);
    vec3 specular = vec3(0.0);

    // Ambient occlusion fully darkens the ambient light, which stands in for light bouncing around: creases
    // and corners get less of it. It also darkens the direct light by aoDirect, which isn't physically right
    // (that light is shadowed already) but makes corners read clearly when there's little ambient
    float ao = (aoEnabled == 1) ? texture(aoMap, gl_FragCoord.xy/aoScreenSize).r : 1.0;
    float directAO = mix(1.0, ao, aoDirect);
    vec3 light = ambient*ao;
    if (bounceEnabled == 1) light += Bounce(bump)*2.0*bounceScale*ao;  // Bounce is indirect light too, so AO darkens it
    vec3 direct = vec3(0.0);

    vec3 toSun = -sunDirection;
    float sunFaceFacing = dot(faceNormal, toSun);
    if (sunFaceFacing > 0.0)
    {
        vec3 sunLight = sunColor*(1.0 - SunShadow(faceNormal, sunFaceFacing));
        float facing = max(dot(normal, toSun), 0.0);
        direct += sunLight*facing;
        if (shiny) specular += sunLight*facing*pow(max(dot(normal, normalize(toSun + toEye)), 0.0), exponent);
    }

    for (int i = 0; i < lightCount; i++)
    {
        vec3 toLight = lightPositions[i] - fragPosition;
        float distance = length(toLight);
        float falloff = clamp(1.0 - distance/lightRadii[i], 0.0, 1.0);
        vec3 lightDir = toLight/max(distance, 0.0001);
        float faceFacing = dot(faceNormal, lightDir);
        if (faceFacing <= 0.0 || falloff <= 0.0) continue;

        float shadow = (pointShadowSlots[i] >= 0) ? PointShadow(i, pointShadowSlots[i], -toLight, faceFacing) : 0.0;
        vec3 pointLight = lightColors[i]*falloff*falloff*(1.0 - shadow);
        float facing = max(dot(normal, lightDir), 0.0);
        direct += pointLight*facing;
        if (shiny) specular += pointLight*facing*pow(max(dot(normal, normalize(lightDir + toEye)), 0.0), exponent);
    }

    light += direct*directAO;
    // Reflection, like Source's $envmap: the nearest cubemap in the mirror direction, masked like the shine
    vec3 mirrored = vec3(0.0);
    if (reflection > 0.0 && cubemapCount > 0) mirrored = ReflectNearestCubemap(reflect(-toEye, normal))*reflection*shineMask*directAO;

    finalColor = vec4(texel.rgb*light + specular*shine*highlightScale*directAO + mirrored, texel.a);
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

    // Hardware shadow filtering: a sampler2DShadow lookup compares the depth against the 4 nearest texels
    // and blends the results by how close each one is, so shadow edges come out smooth instead of in steps
    glBindTexture(GL_TEXTURE_2D_, target.depth.id);
    glTexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_);
    glTexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_LINEAR_);
    glTexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, GL_COMPARE_REF_TO_TEXTURE_);
    glTexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_FUNC_, GL_LEQUAL_);
    glBindTexture(GL_TEXTURE_2D_, 0);

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
    lighting.pointShadows = true;
    lighting.cubemapPositions.clear();
    lighting.cubemapTextures.clear();
    lighting.capturingCubemaps = false;
    lighting.unlit = false;

    // Sun above one corner of the area, shining toward its center, so the light comes in at an angle
    Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
    lighting.sun.position = { bounds.min.x, bounds.max.y + 320.0f, bounds.min.z };
    lighting.sun.direction = Vector3Normalize(Vector3Subtract(center, lighting.sun.position));
    lighting.sun.color = { 255, 244, 214, 255 };  // Slightly warm white
    lighting.sun.intensity = 1.0f;
    lighting.sun.ambient = 0.35f;

    Shader &shader = lighting.shader;
    std::string fragmentSource = TextFormat("#version 330\n#define MAX_LIGHTS %d\n#define MAX_SHADOW_LIGHTS %d\n", MAX_POINT_LIGHTS, MAX_SHADOW_LIGHTS);
    fragmentSource += LIGHTING_FS;
    shader = LoadShaderFromMemory(LIGHTING_VS, fragmentSource.c_str());
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
    lighting.pointShadowSlotsLoc = GetShaderLocation(shader, "pointShadowSlots");
    lighting.pointShadowTileSizeLoc = GetShaderLocation(shader, "pointShadowTileSize");
    lighting.pointShadowRowsLoc = GetShaderLocation(shader, "pointShadowRows");

    lighting.aoMapLoc = GetShaderLocation(shader, "aoMap");
    lighting.aoEnabledLoc = GetShaderLocation(shader, "aoEnabled");
    lighting.aoScreenSizeLoc = GetShaderLocation(shader, "aoScreenSize");
    lighting.aoDirectLoc = GetShaderLocation(shader, "aoDirect");
    lighting.bounceEnabledLoc = GetShaderLocation(shader, "bounceEnabled");
    lighting.bounceScaleLoc = GetShaderLocation(shader, "bounceScale");
    lighting.lightmapLoc = GetShaderLocation(shader, "lightmap");
    lighting.bumpMapLoc = GetShaderLocation(shader, "bumpMap");
    lighting.specularMapLoc = GetShaderLocation(shader, "specularMap");
    lighting.glossMapLoc = GetShaderLocation(shader, "glossMap");
    lighting.hasBumpMapLoc = GetShaderLocation(shader, "hasBumpMap");
    lighting.bumpGreenUpLoc = GetShaderLocation(shader, "bumpGreenUp");
    lighting.hasSpecularMapLoc = GetShaderLocation(shader, "hasSpecularMap");
    lighting.hasGlossMapLoc = GetShaderLocation(shader, "hasGlossMap");
    lighting.specularStrengthLoc = GetShaderLocation(shader, "specularStrength");
    lighting.glossinessLoc = GetShaderLocation(shader, "glossiness");
    lighting.viewPositionLoc = GetShaderLocation(shader, "viewPosition");
    lighting.reflectionLoc = GetShaderLocation(shader, "reflection");
    lighting.unlitLoc = GetShaderLocation(shader, "unlit");
    for (int i = 0; i < MAX_CUBEMAPS; i++) lighting.cubemapLocs[i] = GetShaderLocation(shader, TextFormat("cubemap%d", i));
    lighting.cubemapPositionsLoc = GetShaderLocation(shader, "cubemapPositions");
    lighting.cubemapCountLoc = GetShaderLocation(shader, "cubemapCount");
    lighting.aoTexture = 0;
    lighting.aoScreenSize = { 1.0f, 1.0f };
    lighting.aoDirect = 0.0f;
    lighting.bounceScale = 1.0f;
    lighting.lightmapTexture = 0;

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
    lighting.pointShadowRows = 4;  // Grows as levels add lights, see RenderPointShadows
    lighting.pointShadowAtlas = LoadShadowMapTexture(6*POINT_SHADOW_TILE_SIZE, lighting.pointShadowRows*POINT_SHADOW_TILE_SIZE);
    lighting.sunViewProjection = MatrixIdentity();
    lighting.shadowDepthScale = 1.0f;
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
static void RenderPointShadows(Lighting &lighting, const std::vector<ShadowCaster> &casters)
{
    for (int &slot : lighting.pointShadowSlots) slot = -1;
    if (!lighting.pointShadows) return;

    // Looking down +X, -X, +Y, -Y, +Z, -Z, in the order the shader picks faces in
    const Vector3 directions[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const Vector3 ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };

    // More shadowed lights than the atlas has rows for: remake it with twice the rows, as often as needed.
    // It never shrinks, so adding and removing lights in the editor doesn't remake it back and forth
    int wanted = (int)fminf((float)lighting.lights.size(), (float)MAX_SHADOW_LIGHTS);
    if (wanted > lighting.pointShadowRows)
    {
        while (lighting.pointShadowRows < wanted) lighting.pointShadowRows *= 2;
        if (lighting.pointShadowRows > MAX_SHADOW_LIGHTS) lighting.pointShadowRows = MAX_SHADOW_LIGHTS;
        rlUnloadTexture(lighting.pointShadowAtlas.depth.id);
        rlUnloadFramebuffer(lighting.pointShadowAtlas.id);
        lighting.pointShadowAtlas = LoadShadowMapTexture(6*POINT_SHADOW_TILE_SIZE, lighting.pointShadowRows*POINT_SHADOW_TILE_SIZE);
    }

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

                rlViewport(face*POINT_SHADOW_TILE_SIZE, slot*POINT_SHADOW_TILE_SIZE, POINT_SHADOW_TILE_SIZE, POINT_SHADOW_TILE_SIZE);
                rlSetMatrixProjection(projection);
                rlSetMatrixModelview(view);
                for (const ShadowCaster &caster : casters)
                {
                    for (int m = 0; m < caster.model->meshCount; m++) DrawMesh(caster.model->meshes[m], lighting.pointDepthMaterial, caster.transform);
                }
            }
        }

        rlSetCullFace(RL_CULL_FACE_BACK);
        rlDisableDepthTest();
    EndTextureMode();
}

void RenderShadowMap(Lighting &lighting, const std::vector<ShadowCaster> &casters)
{
    Vector3 direction = Vector3Normalize(lighting.sun.direction);
    Vector3 center = Vector3Scale(Vector3Add(lighting.bounds.min, lighting.bounds.max), 0.5f);
    float radius = Vector3Length(Vector3Subtract(lighting.bounds.max, lighting.bounds.min))*0.5f;

    // Orthographic camera looking along the sun's direction, framing the whole area. Its clip planes are fitted
    // tightly around the area, so the shadow map's depth precision isn't spread over empty space
    Camera camera = { 0 };
    camera.position = Vector3Subtract(center, Vector3Scale(direction, radius + 32.0f));
    camera.target = center;
    camera.up = (fabsf(direction.y) > 0.99f) ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = radius*2.0f;
    camera.projection = CAMERA_ORTHOGRAPHIC;

    const double nearPlane = 3.2;
    const double farPlane = 2.0*radius + 64.0;
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
            for (const ShadowCaster &caster : casters)
            {
                for (int i = 0; i < caster.model->meshCount; i++) DrawMesh(caster.model->meshes[i], lighting.depthMaterial, caster.transform);
            }
            rlSetCullFace(RL_CULL_FACE_BACK);
        EndMode3D();
    EndTextureMode();

    rlSetClipPlanes(oldNear, oldFar);

    RenderPointShadows(lighting, casters);
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
        radii[count] = fmaxf(light.radius, 0.032f);
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
    SetShaderValue(shader, lighting.pointShadowRowsLoc, &lighting.pointShadowRows, SHADER_UNIFORM_INT);
    SetShaderValueV(shader, lighting.pointShadowSlotsLoc, lighting.pointShadowSlots, SHADER_UNIFORM_INT, MAX_POINT_LIGHTS);

    // Ambient occlusion: off until SetLightingAO turns it on
    int aoSlot = AO_SLOT;
    int aoEnabled = 0;
    SetShaderValue(shader, lighting.aoMapLoc, &aoSlot, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.aoEnabledLoc, &aoEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.aoScreenSizeLoc, &lighting.aoScreenSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, lighting.aoDirectLoc, &lighting.aoDirect, SHADER_UNIFORM_FLOAT);

    // Bounce: off until SetLightingBounce turns it on
    int bounceEnabled = 0;
    SetShaderValue(shader, lighting.bounceEnabledLoc, &bounceEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.bounceScaleLoc, &lighting.bounceScale, SHADER_UNIFORM_FLOAT);
    int lightmapSlot = LIGHTMAP_SLOT;
    SetShaderValue(shader, lighting.lightmapLoc, &lightmapSlot, SHADER_UNIFORM_INT);

    // Materials: none until SetLightingMaterial gives the next mesh one. The camera is where the view
    // matrix BeginMode3D set up puts it, for highlights
    int bumpSlot = BUMP_MAP_SLOT, specularSlot = SPECULAR_MAP_SLOT, glossSlot = GLOSS_MAP_SLOT;
    SetShaderValue(shader, lighting.bumpMapLoc, &bumpSlot, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.specularMapLoc, &specularSlot, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.glossMapLoc, &glossSlot, SHADER_UNIFORM_INT);
    Matrix cameraToWorld = MatrixInvert(rlGetMatrixModelview());
    Vector3 viewPosition = { cameraToWorld.m12, cameraToWorld.m13, cameraToWorld.m14 };
    SetShaderValue(shader, lighting.viewPositionLoc, &viewPosition, SHADER_UNIFORM_VEC3);
    int unlit = lighting.unlit ? 1 : 0;
    SetShaderValue(shader, lighting.unlitLoc, &unlit, SHADER_UNIFORM_INT);
    ClearLightingMaterial(lighting);

    // Cubemaps: the ones nearest the camera, none while capturing them. Every sampler gets its own slot
    // even when unused, since two samplers of different kinds on one slot would stop anything drawing
    std::vector<int> nearest;
    if (!lighting.capturingCubemaps)
    {
        for (int i = 0; i < (int)lighting.cubemapPositions.size(); i++) nearest.push_back(i);
        std::sort(nearest.begin(), nearest.end(), [&](int a, int b)
        {
            return Vector3DistanceSqr(lighting.cubemapPositions[a], viewPosition) < Vector3DistanceSqr(lighting.cubemapPositions[b], viewPosition);
        });
        if (nearest.size() > MAX_CUBEMAPS) nearest.resize(MAX_CUBEMAPS);
    }
    Vector3 cubemapPositions[MAX_CUBEMAPS] = {};
    for (int i = 0; i < MAX_CUBEMAPS; i++)
    {
        int slot = FIRST_CUBEMAP_SLOT + i;
        SetShaderValue(shader, lighting.cubemapLocs[i], &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(slot);
        if (i < (int)nearest.size())
        {
            cubemapPositions[i] = lighting.cubemapPositions[nearest[i]];
            rlEnableTextureCubemap(lighting.cubemapTextures[nearest[i]]);
        }
        else rlDisableTextureCubemap();
    }
    rlActiveTextureSlot(0);
    int cubemapCount = (int)nearest.size();
    SetShaderValueV(shader, lighting.cubemapPositionsLoc, cubemapPositions, SHADER_UNIFORM_VEC3, MAX_CUBEMAPS);
    SetShaderValue(shader, lighting.cubemapCountLoc, &cubemapCount, SHADER_UNIFORM_INT);

    rlActiveTextureSlot(SHADOW_MAP_SLOT);
    rlEnableTexture(lighting.shadowMap.depth.id);
    rlActiveTextureSlot(POINT_SHADOW_ATLAS_SLOT);
    rlEnableTexture(lighting.pointShadowAtlas.depth.id);
    rlActiveTextureSlot(AO_SLOT);
    rlEnableTexture(lighting.aoTexture);
    rlActiveTextureSlot(LIGHTMAP_SLOT);
    rlEnableTexture(lighting.lightmapTexture);
    rlActiveTextureSlot(0);
}

void SetLightingAO(const Lighting &lighting, bool enabled)
{
    int on = (enabled && lighting.aoTexture != 0) ? 1 : 0;
    SetShaderValue(lighting.shader, lighting.aoEnabledLoc, &on, SHADER_UNIFORM_INT);
}

void SetLightingBounce(const Lighting &lighting, bool enabled)
{
    int on = enabled ? 1 : 0;
    SetShaderValue(lighting.shader, lighting.bounceEnabledLoc, &on, SHADER_UNIFORM_INT);
}

void SetLightingMaterial(const Lighting &lighting, unsigned int bumpMap, unsigned int specularMap, unsigned int glossMap,
                         bool bumpGreenUp, float specular, float gloss, float reflection)
{
    const Shader &shader = lighting.shader;
    int hasBump = (bumpMap != 0), hasSpecular = (specularMap != 0), hasGloss = (glossMap != 0), greenUp = bumpGreenUp;
    SetShaderValue(shader, lighting.hasBumpMapLoc, &hasBump, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.bumpGreenUpLoc, &greenUp, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.hasSpecularMapLoc, &hasSpecular, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.hasGlossMapLoc, &hasGloss, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lighting.specularStrengthLoc, &specular, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lighting.glossinessLoc, &gloss, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lighting.reflectionLoc, &reflection, SHADER_UNIFORM_FLOAT);

    rlActiveTextureSlot(BUMP_MAP_SLOT);
    rlEnableTexture(bumpMap);
    rlActiveTextureSlot(SPECULAR_MAP_SLOT);
    rlEnableTexture(specularMap);
    rlActiveTextureSlot(GLOSS_MAP_SLOT);
    rlEnableTexture(glossMap);
    rlActiveTextureSlot(0);
}

void ClearLightingMaterial(const Lighting &lighting)
{
    SetLightingMaterial(lighting, 0, 0, 0, false, 0.0f, 0.5f, 0.0f);
}

void EndLighting()
{
    for (int i = 0; i < MAX_CUBEMAPS; i++)
    {
        rlActiveTextureSlot(FIRST_CUBEMAP_SLOT + i);
        rlDisableTextureCubemap();
    }
    rlActiveTextureSlot(BUMP_MAP_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(SPECULAR_MAP_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(GLOSS_MAP_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(SHADOW_MAP_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(POINT_SHADOW_ATLAS_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(AO_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(LIGHTMAP_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(0);
}
