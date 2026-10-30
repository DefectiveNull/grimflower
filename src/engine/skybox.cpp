#include "skybox.h"
#include "rlgl.h"

#include <cmath>

// The prebuilt raylib can't load .hdr files, so decode them with stb_image. Static, so this copy
// doesn't clash with the one in world.cpp (which only does TGA) or raylib's own
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#include "stb_image.h"

// A cube around the camera. Each pixel looks up the image by its direction from the camera
static const char *SKY_VS = R"(
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
out vec3 direction;
void main()
{
    direction = vertexPosition;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

static const char *SKY_FS = R"(
#version 330
in vec3 direction;
uniform sampler2D texture0;
uniform float exposure;
out vec4 finalColor;

const float PI = 3.14159265;

// Squeezes HDR brightness into 0..1 with a filmic curve (ACES, fitted by Krzysztof Narkowicz)
vec3 Tonemap(vec3 x)
{
    return clamp((x*(2.51*x + 0.03))/(x*(2.43*x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    // Equirectangular lookup: around the horizon for u, from straight up to straight down for v.
    // u = 0.5 is straight ahead down -Z, the same way the player faces at angle 0
    vec3 d = normalize(direction);
    vec2 uv = vec2(0.5 + atan(d.x, -d.z)/(2.0*PI), acos(clamp(d.y, -1.0, 1.0))/PI);

    vec3 color = Tonemap(texture(texture0, uv).rgb*exposure);
    finalColor = vec4(pow(color, vec3(1.0/2.2)), 1.0);  // The HDR is linear light, the screen expects gamma
}
)";

// The sky's average color, over its upper half (the top half of the image, down to the horizon), so the ground
// in the image doesn't tint it: each row weighted by how much of the sphere it covers (sin of its angle from
// straight up). Each pixel counts no brighter than white, keeping its hue, so a
// bright HDR sun doesn't drown out the sky around it. Then brightened until its brightest channel is 1, and put
// in the screen's gamma like the sky's own pixels, so it's the color you'd pick for the sky by eye
static Vector3 AverageSkyColor(const float *pixels, int width, int height)
{
    double sum[3] = { 0.0, 0.0, 0.0 };
    for (int y = 0; y < (height + 1)/2; y++)
    {
        double weight = sin(PI*(y + 0.5f)/height);
        for (int x = 0; x < width; x++)
        {
            const float *p = pixels + ((size_t)y*width + x)*3;
            float brightest = fmaxf(p[0], fmaxf(p[1], p[2]));
            float scale = (brightest > 1.0f) ? 1.0f/brightest : 1.0f;
            for (int c = 0; c < 3; c++) sum[c] += weight*fmaxf(p[c], 0.0f)*scale;
        }
    }

    double brightest = fmax(sum[0], fmax(sum[1], sum[2]));
    if (brightest <= 0.0) return { 1.0f, 1.0f, 1.0f };  // A black sky has no color to give
    return { powf((float)(sum[0]/brightest), 1.0f/2.2f), powf((float)(sum[1]/brightest), 1.0f/2.2f), powf((float)(sum[2]/brightest), 1.0f/2.2f) };
}

bool LoadSkybox(Skybox &skybox, const char *path)
{
    UnloadSkybox(skybox);

    // Read through raylib, so it comes from the project folder or gamedata.zip
    int fileSize = 0;
    unsigned char *fileData = LoadFileData(path, &fileSize);
    if (fileData == nullptr) return false;

    int width, height, channels;
    float *pixels = stbi_loadf_from_memory(fileData, fileSize, &width, &height, &channels, 3);
    UnloadFileData(fileData);
    if (pixels == nullptr)
    {
        TraceLog(LOG_WARNING, "SKYBOX: [%s] Couldn't decode: %s", path, stbi_failure_reason());
        return false;
    }

    Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32 };
    skybox.texture = LoadTextureFromImage(image);
    skybox.averageColor = AverageSkyColor(pixels, width, height);
    stbi_image_free(pixels);
    SetTextureFilter(skybox.texture, TEXTURE_FILTER_BILINEAR);

    skybox.shader = LoadShaderFromMemory(SKY_VS, SKY_FS);
    skybox.exposureLoc = GetShaderLocation(skybox.shader, "exposure");
    skybox.exposure = 1.0f;

    skybox.cube = LoadModelFromMesh(GenMeshCube(64.0f, 64.0f, 64.0f));
    skybox.cube.materials[0].shader = skybox.shader;
    skybox.cube.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = skybox.texture;

    skybox.path = path;
    skybox.loaded = true;
    TraceLog(LOG_INFO, "SKYBOX: [%s] Loaded (%dx%d), average color %.2f %.2f %.2f", path, width, height,
             skybox.averageColor.x, skybox.averageColor.y, skybox.averageColor.z);
    return true;
}

void UnloadSkybox(Skybox &skybox)
{
    if (!skybox.loaded) return;

    // UnloadModel leaves the shader and texture alone, so they're unloaded separately
    UnloadModel(skybox.cube);
    UnloadShader(skybox.shader);
    UnloadTexture(skybox.texture);
    skybox = Skybox();
}

void DrawSkybox(const Skybox &skybox, Camera camera)
{
    if (!skybox.loaded) return;

    SetShaderValue(skybox.shader, skybox.exposureLoc, &skybox.exposure, SHADER_UNIFORM_FLOAT);

    // Centered on the camera so it never gets closer, seen from inside, and leaving the depth buffer
    // alone so the world always draws over it
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawModel(skybox.cube, camera.position, 1.0f, WHITE);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}
