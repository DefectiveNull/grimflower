#include "postfx.h"
#include "world.h"
#include "raymath.h"

// Draws the 3D view through the grade. Colors are the screen's (gamma), as the lighting shader wrote them
static const char *POST_FS = R"(
#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D gradient;
uniform float gradientStrength;
uniform float contrast;
uniform float saturation;
uniform float gamma;
uniform float filmGrain;
uniform float time;
out vec4 finalColor;

float Luma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// A random number from 0 to 1 for a pixel (Dave Hoskins' hash without sine)
float Hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx)*0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y)*p3.z);
}

void main()
{
    vec3 color = texture(texture0, fragTexCoord).rgb;

    // The level's gradient map: brightness picks the color, along the gradient's middle row
    if (gradientStrength > 0.0)
    {
        vec3 graded = texture(gradient, vec2(clamp(Luma(color), 0.0, 1.0), 0.5)).rgb;
        color = mix(color, graded, gradientStrength);
    }

    // Contrast around middle gray, then saturation away from (or toward) each pixel's own gray
    color = (color - 0.5)*contrast + 0.5;
    color = clamp(mix(vec3(Luma(color)), color, saturation), 0.0, 1.0);

    // Gamma bends the brightness curve, leaving black and white where they are
    color = pow(color, vec3(1.0/gamma));

    // Film grain: new noise every frame, strongest in the midtones like film's, where it shows most
    if (filmGrain > 0.0)
    {
        float noise = Hash(gl_FragCoord.xy + fract(time*7.31)*1024.0) - 0.5;
        float midtones = 1.0 - pow(abs(Luma(color)*2.0 - 1.0), 2.0);
        color += noise*filmGrain*0.25*(0.35 + 0.65*midtones);
    }

    finalColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

void BeginPostProcess(PostProcess &post, int width, int height)
{
    if (!post.ready)
    {
        post.shader = LoadShaderFromMemory(nullptr, POST_FS);
        post.gradientLoc = GetShaderLocation(post.shader, "gradient");
        post.gradientStrengthLoc = GetShaderLocation(post.shader, "gradientStrength");
        post.contrastLoc = GetShaderLocation(post.shader, "contrast");
        post.saturationLoc = GetShaderLocation(post.shader, "saturation");
        post.gammaLoc = GetShaderLocation(post.shader, "gamma");
        post.filmGrainLoc = GetShaderLocation(post.shader, "filmGrain");
        post.timeLoc = GetShaderLocation(post.shader, "time");
        post.ready = true;
    }
    if (post.target.id == 0 || post.target.texture.width != width || post.target.texture.height != height)
    {
        if (post.target.id != 0) UnloadRenderTexture(post.target);
        post.target = LoadRenderTexture(width, height);
    }
    BeginTextureMode(post.target);
}

void EndPostProcess(const PostProcess &post, const ColorGrade &grade, const PostSettings &settings)
{
    EndTextureMode();

    float strength = (grade.gradient.id != 0) ? Clamp(grade.strength, 0.0f, 1.0f) : 0.0f;
    float contrast = Clamp(settings.contrast, 0.0f, 4.0f);
    float saturation = Clamp(settings.saturation, 0.0f, 4.0f);
    float filmGrain = Clamp(settings.filmGrain, 0.0f, 1.0f);
    float gamma = Clamp(settings.gamma, 0.25f, 4.0f);
    float time = (float)GetTime();

    BeginShaderMode(post.shader);
        SetShaderValue(post.shader, post.gradientStrengthLoc, &strength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post.shader, post.contrastLoc, &contrast, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post.shader, post.saturationLoc, &saturation, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post.shader, post.gammaLoc, &gamma, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post.shader, post.filmGrainLoc, &filmGrain, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post.shader, post.timeLoc, &time, SHADER_UNIFORM_FLOAT);
        if (grade.gradient.id != 0) SetShaderValueTexture(post.shader, post.gradientLoc, grade.gradient);

        // Render textures are upside down, so it's drawn flipped
        const Texture2D &view = post.target.texture;
        DrawTextureRec(view, { 0.0f, 0.0f, (float)view.width, -(float)view.height }, { 0.0f, 0.0f }, WHITE);
    EndShaderMode();
}

void UnloadPostProcess(PostProcess &post)
{
    if (post.target.id != 0) UnloadRenderTexture(post.target);
    if (post.ready) UnloadShader(post.shader);
    post = PostProcess();
}

bool LoadColorGrade(ColorGrade &grade, const char *gradientPath, Color shadows, Color highlights, float strength)
{
    UnloadColorGrade(grade);
    grade.strength = strength;

    bool loaded = false;
    if (gradientPath != nullptr && gradientPath[0] != '\0')
    {
        loaded = LoadGameTexture(gradientPath, grade.gradient);
        if (!loaded) grade.gradient = { 0 };
    }
    if (!loaded)
    {
        // The two colors, blended across 256 pixels
        Image image = GenImageColor(256, 1, shadows);
        for (int x = 0; x < 256; x++) ImageDrawPixel(&image, x, 0, ColorLerp(shadows, highlights, x/255.0f));
        grade.gradient = LoadTextureFromImage(image);
        UnloadImage(image);
    }

    // Read between its pixels, and never wrapped round from one end to the other
    SetTextureFilter(grade.gradient, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(grade.gradient, TEXTURE_WRAP_CLAMP);
    return loaded || gradientPath == nullptr || gradientPath[0] == '\0';
}

void UnloadColorGrade(ColorGrade &grade)
{
    if (grade.gradient.id != 0) UnloadTexture(grade.gradient);
    grade = ColorGrade();
}
