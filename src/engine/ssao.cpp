#include "ssao.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>

static const int SAMPLE_COUNT = 16;       // Must match the shader

static const float BIAS = 0.8f;           // Stops flat surfaces occluding themselves

// Pass 1: what the world mesh looks like from the camera, as view space normals and distance
static const char *GBUFFER_VS = R"(
#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matView;
uniform mat4 matModel;
out vec3 viewPosition;
out vec3 viewNormal;
void main()
{
    mat4 modelView = matView*matModel;
    viewPosition = vec3(modelView*vec4(vertexPosition, 1.0));
    viewNormal = mat3(modelView)*vertexNormal;  // The world model's only scaled evenly, so this keeps normals straight
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

static const char *GBUFFER_FS = R"(
#version 330
in vec3 viewPosition;
in vec3 viewNormal;
out vec4 finalColor;
void main()
{
    finalColor = vec4(normalize(viewNormal), -viewPosition.z);
}
)";

// Shared by the full screen passes: a rectangle over the whole target
static const char *FULLSCREEN_VS = R"(
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main()
{
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

// Pass 2: for each pixel, how many points in a hemisphere around its normal are behind other geometry
static const char *SSAO_FS = R"(
#version 330
#define SAMPLE_COUNT 16
uniform sampler2D gbuffer;
uniform sampler2D noiseMap;
uniform vec3 samples[SAMPLE_COUNT];
uniform mat4 projection;
uniform vec2 projScale;    // Half the view's width and height at a distance of 1
uniform vec2 screenSize;
uniform float radius;
uniform float bias;
out vec4 finalColor;

vec3 ViewPosition(vec2 uv, float distance)
{
    return vec3((uv*2.0 - 1.0)*projScale*distance, -distance);
}

void main()
{
    vec2 uv = gl_FragCoord.xy/screenSize;
    vec4 here = texture(gbuffer, uv);
    if (here.w <= 0.0)
    {
        finalColor = vec4(1.0);  // Nothing here, like the sky
        return;
    }

    vec3 position = ViewPosition(uv, here.w);
    vec3 normal = normalize(here.xyz);

    // A hemisphere around the normal, turned by the noise so neighbouring pixels sample differently
    vec3 random = texture(noiseMap, gl_FragCoord.xy/4.0).xyz*2.0 - 1.0;
    vec3 tangent = normalize(random - normal*dot(random, normal));
    mat3 toView = mat3(tangent, cross(normal, tangent), normal);

    float occlusion = 0.0;
    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        vec3 point = position + toView*samples[i]*radius;
        vec4 clip = projection*vec4(point, 1.0);
        vec2 pointUV = clip.xy/clip.w*0.5 + 0.5;
        float sceneDistance = texture(gbuffer, pointUV).w;
        if (sceneDistance <= 0.0) continue;

        // Something nearer to the camera than the sample point covers it. Geometry far in front of this
        // pixel shouldn't darken it, so that fades out past the radius
        float inRange = smoothstep(0.0, 1.0, radius/abs(here.w - sceneDistance));
        occlusion += ((sceneDistance <= -point.z - bias) ? 1.0 : 0.0)*inRange;
    }

    float ao = 1.0 - occlusion/float(SAMPLE_COUNT);
    finalColor = vec4(ao, ao, ao, 1.0);
}
)";

// Pass 3: averages 4x4 pixels, the size of the noise pattern, which smooths it away
static const char *BLUR_FS = R"(
#version 330
uniform sampler2D source;
uniform vec2 screenSize;
out vec4 finalColor;
void main()
{
    vec2 texel = 1.0/screenSize;
    vec2 uv = gl_FragCoord.xy*texel;
    float sum = 0.0;
    for (int x = -2; x < 2; x++)
    {
        for (int y = -2; y < 2; y++) sum += texture(source, uv + vec2(x, y)*texel).r;  // Pixel centres, all 16 of them
    }
    float ao = sum/16.0;
    finalColor = vec4(ao, ao, ao, 1.0);
}
)";

// A target with a 32-bit float color texture, precise enough for distances, and a depth buffer
static RenderTexture2D LoadFloatTarget(int width, int height)
{
    RenderTexture2D target = { 0 };
    target.id = rlLoadFramebuffer();
    rlEnableFramebuffer(target.id);

    target.texture.id = rlLoadTexture(nullptr, width, height, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    target.texture.mipmaps = 1;
    rlTextureParameters(target.texture.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(target.texture.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(target.texture.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(target.texture.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    target.depth.id = rlLoadTextureDepth(width, height, true);
    target.depth.width = width;
    target.depth.height = height;
    rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);

    if (!rlFramebufferComplete(target.id)) TraceLog(LOG_WARNING, "SSAO: The normal and depth framebuffer is incomplete");
    rlDisableFramebuffer();
    return target;
}

static RenderTexture2D LoadAOTarget(int width, int height)
{
    RenderTexture2D target = LoadRenderTexture(width, height);
    SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
    return target;
}

// Small random numbers that are the same every run, so the look doesn't change between runs
static float Random01(unsigned int &state)
{
    state = state*1664525u + 1013904223u;
    return (float)(state >> 8)/16777216.0f;
}

static void LoadAmbientOcclusion(AmbientOcclusion &ao, int width, int height)
{
    ao.width = width;
    ao.height = height;
    ao.gbuffer = LoadFloatTarget(width, height);
    ao.raw = LoadAOTarget(width, height);
    ao.blurred = LoadAOTarget(width, height);

    ao.gbufferShader = LoadShaderFromMemory(GBUFFER_VS, GBUFFER_FS);
    ao.gbufferShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(ao.gbufferShader, "matModel");
    ao.gbufferShader.locs[SHADER_LOC_MATRIX_VIEW] = GetShaderLocation(ao.gbufferShader, "matView");
    ao.gbufferMaterial = LoadMaterialDefault();
    ao.gbufferMaterial.shader = ao.gbufferShader;

    ao.ssaoShader = LoadShaderFromMemory(FULLSCREEN_VS, SSAO_FS);
    ao.ssaoGbufferLoc = GetShaderLocation(ao.ssaoShader, "gbuffer");
    ao.ssaoNoiseLoc = GetShaderLocation(ao.ssaoShader, "noiseMap");
    ao.ssaoProjectionLoc = GetShaderLocation(ao.ssaoShader, "projection");
    ao.ssaoProjScaleLoc = GetShaderLocation(ao.ssaoShader, "projScale");
    ao.ssaoScreenSizeLoc = GetShaderLocation(ao.ssaoShader, "screenSize");
    ao.ssaoRadiusLoc = GetShaderLocation(ao.ssaoShader, "radius");

    // Sample points in a hemisphere of radius 1 pointing up z, more of them close to the middle, where
    // occlusion matters most
    unsigned int state = 12345u;
    Vector3 samples[SAMPLE_COUNT];
    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        Vector3 s = { Random01(state)*2.0f - 1.0f, Random01(state)*2.0f - 1.0f, Random01(state) };
        s = Vector3Scale(Vector3Normalize(s), Random01(state));
        float t = (float)i/SAMPLE_COUNT;
        samples[i] = Vector3Scale(s, Lerp(0.1f, 1.0f, t*t));
    }
    SetShaderValueV(ao.ssaoShader, GetShaderLocation(ao.ssaoShader, "samples"), samples, SHADER_UNIFORM_VEC3, SAMPLE_COUNT);
    float bias = BIAS;
    SetShaderValue(ao.ssaoShader, GetShaderLocation(ao.ssaoShader, "bias"), &bias, SHADER_UNIFORM_FLOAT);

    // 4x4 random directions around z, stored as colors
    Color pixels[16];
    for (Color &p : pixels)
    {
        Vector3 v = Vector3Normalize({ Random01(state)*2.0f - 1.0f, Random01(state)*2.0f - 1.0f, 0.0f });
        p = { (unsigned char)((v.x*0.5f + 0.5f)*255.0f), (unsigned char)((v.y*0.5f + 0.5f)*255.0f), 128, 255 };
    }
    Image noise = { pixels, 4, 4, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    ao.noise = LoadTextureFromImage(noise);
    SetTextureWrap(ao.noise, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(ao.noise, TEXTURE_FILTER_POINT);

    ao.blurShader = LoadShaderFromMemory(FULLSCREEN_VS, BLUR_FS);
    ao.blurSourceLoc = GetShaderLocation(ao.blurShader, "source");
    ao.blurScreenSizeLoc = GetShaderLocation(ao.blurShader, "screenSize");

    ao.ready = true;
}

void UnloadAmbientOcclusion(AmbientOcclusion &ao)
{
    if (!ao.ready) return;
    UnloadRenderTexture(ao.gbuffer);
    UnloadRenderTexture(ao.raw);
    UnloadRenderTexture(ao.blurred);
    UnloadMaterial(ao.gbufferMaterial);  // Also unloads gbufferShader
    UnloadShader(ao.ssaoShader);
    UnloadShader(ao.blurShader);
    UnloadTexture(ao.noise);
    ao = AmbientOcclusion();
}

// A full screen pass: draws a rectangle over the target with the shader, which reads gl_FragCoord
static void BeginFullscreenPass(RenderTexture2D target, Shader shader)
{
    BeginTextureMode(target);
    ClearBackground(WHITE);
    BeginShaderMode(shader);
}

static void EndFullscreenPass(RenderTexture2D target)
{
    DrawRectangle(0, 0, target.texture.width, target.texture.height, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void RenderAmbientOcclusion(AmbientOcclusion &ao, const Model &model, Camera camera, int width, int height, float radius)
{
    if (ao.ready && (ao.width != width || ao.height != height)) UnloadAmbientOcclusion(ao);
    if (!ao.ready) LoadAmbientOcclusion(ao, width, height);

    // 1. Normals and distance, from the same view the frame is drawn from. Without blending: the distance
    // is in the alpha channel, and alpha blending would mix it into everything
    BeginTextureMode(ao.gbuffer);
        rlClearColor(0, 0, 0, 0);
        rlClearScreenBuffers();
        rlDisableColorBlend();
        BeginMode3D(camera);
            for (int i = 0; i < model.meshCount; i++) DrawMesh(model.meshes[i], ao.gbufferMaterial, model.transform);
        EndMode3D();
        rlEnableColorBlend();
    EndTextureMode();

    // The projection the frame uses, to find where each sample point lands on screen
    float aspect = (float)width/(float)height;
    Matrix projection = MatrixPerspective(camera.fovy*DEG2RAD, aspect, rlGetCullDistanceNear(), rlGetCullDistanceFar());
    float halfHeight = tanf(camera.fovy*DEG2RAD*0.5f);
    Vector2 projScale = { halfHeight*aspect, halfHeight };
    Vector2 screenSize = { (float)width, (float)height };

    // 2. Occlusion
    BeginFullscreenPass(ao.raw, ao.ssaoShader);
        SetShaderValueTexture(ao.ssaoShader, ao.ssaoGbufferLoc, ao.gbuffer.texture);
        SetShaderValueTexture(ao.ssaoShader, ao.ssaoNoiseLoc, ao.noise);
        SetShaderValueMatrix(ao.ssaoShader, ao.ssaoProjectionLoc, projection);
        SetShaderValue(ao.ssaoShader, ao.ssaoProjScaleLoc, &projScale, SHADER_UNIFORM_VEC2);
        SetShaderValue(ao.ssaoShader, ao.ssaoScreenSizeLoc, &screenSize, SHADER_UNIFORM_VEC2);
        SetShaderValue(ao.ssaoShader, ao.ssaoRadiusLoc, &radius, SHADER_UNIFORM_FLOAT);
    EndFullscreenPass(ao.raw);

    // 3. Smoothed
    BeginFullscreenPass(ao.blurred, ao.blurShader);
        SetShaderValueTexture(ao.blurShader, ao.blurSourceLoc, ao.raw.texture);
        SetShaderValue(ao.blurShader, ao.blurScreenSizeLoc, &screenSize, SHADER_UNIFORM_VEC2);
    EndFullscreenPass(ao.blurred);
}
