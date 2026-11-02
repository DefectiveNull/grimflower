#include "volumes.h"
#include "audio.h"
#include "game.h"
#include "lighting.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>

// Water is drawn with this texture, a placeholder until there's a real one. Ladders and triggers are invisible
static const char *WATER_TEXTURE = "textures/liquids/water.tga";
static const float WATER_TILE_SIZE = 64.0f;  // World units per repeat of the texture, like Quake's 64x64 water
static const float WATER_ALPHA = 0.75f;

// Quake's water turbulence (R_DrawWaterSurfaces): each texture axis wobbles with a sine of the other.
// Quake moves by 8 texels at 0.125 radians per texel on a 64 texel texture: 1/8 of a tile, 8 radians per tile
static const char *WATER_VS = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 fragTexCoord;
void main()
{
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

static const char *WATER_FS = R"(
#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float time;
uniform float alpha;
out vec4 finalColor;
void main()
{
    vec2 uv = fragTexCoord + 0.125*sin(fragTexCoord.yx*8.0 + time);
    finalColor = vec4(texture(texture0, uv).rgb, alpha);
}
)";

static bool PointInBox(Vector3 p, BoundingBox box)
{
    return p.x >= box.min.x && p.x <= box.max.x && p.y >= box.min.y && p.y <= box.max.y && p.z >= box.min.z && p.z <= box.max.z;
}

int PointContents(const Volumes &volumes, Vector3 point)
{
    int contents = CONTENTS_EMPTY;
    for (const Volume &volume : volumes.volumes)
    {
        if (PointInBox(point, volume.box)) contents |= volume.contents;
    }
    return contents;
}

int BoxContents(const Volumes &volumes, BoundingBox box)
{
    int contents = CONTENTS_EMPTY;
    for (const Volume &volume : volumes.volumes)
    {
        if (CheckCollisionBoxes(box, volume.box)) contents |= volume.contents;
    }
    return contents;
}

// ---------------------------------------------------------------------------------------------
// Water

// The box's six faces as triangles. The texture coordinates come from world positions, so the texture
// tiles at the same size on every face and lines up between neighbouring water volumes
static Mesh BuildWaterMesh(BoundingBox box)
{
    Vector3 a = box.min, b = box.max;
    const Vector3 corners[6][4] = {
        { { a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z } },  // Top, the surface
        { { a.x, a.y, a.z }, { a.x, a.y, b.z }, { b.x, a.y, b.z }, { b.x, a.y, a.z } },  // Bottom
        { { a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, b.y, a.z }, { a.x, b.y, a.z } },  // -Z
        { { a.x, a.y, b.z }, { a.x, b.y, b.z }, { b.x, b.y, b.z }, { b.x, a.y, b.z } },  // +Z
        { { a.x, a.y, a.z }, { a.x, b.y, a.z }, { a.x, b.y, b.z }, { a.x, a.y, b.z } },  // -X
        { { b.x, a.y, a.z }, { b.x, a.y, b.z }, { b.x, b.y, b.z }, { b.x, b.y, a.z } },  // +X
    };

    Mesh mesh = { 0 };
    mesh.vertexCount = 6*6;
    mesh.triangleCount = 6*2;
    mesh.vertices = (float *)RL_MALLOC(mesh.vertexCount*3*sizeof(float));   // UnloadModel frees these with RL_FREE
    mesh.texcoords = (float *)RL_MALLOC(mesh.vertexCount*2*sizeof(float));

    const int order[6] = { 0, 1, 2, 0, 2, 3 };
    int v = 0;
    for (int face = 0; face < 6; face++)
    {
        for (int i : order)
        {
            Vector3 p = corners[face][i];
            mesh.vertices[v*3 + 0] = p.x;
            mesh.vertices[v*3 + 1] = p.y;
            mesh.vertices[v*3 + 2] = p.z;

            // Top and bottom use x and z, the sides the horizontal axis along them and y
            Vector2 uv;
            if (face < 2) uv = { p.x, p.z };
            else if (face < 4) uv = { p.x, -p.y };
            else uv = { p.z, -p.y };
            mesh.texcoords[v*2 + 0] = uv.x/WATER_TILE_SIZE;
            mesh.texcoords[v*2 + 1] = uv.y/WATER_TILE_SIZE;
            v++;
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}

void AddWaterSurface(Volumes &volumes, Volume &volume)
{
    if (!volumes.waterLoaded)
    {
        volumes.waterShader = LoadShaderFromMemory(WATER_VS, WATER_FS);
        volumes.waterTimeLoc = GetShaderLocation(volumes.waterShader, "time");
        float alpha = WATER_ALPHA;
        SetShaderValue(volumes.waterShader, GetShaderLocation(volumes.waterShader, "alpha"), &alpha, SHADER_UNIFORM_FLOAT);
        if (!LoadGameTexture(WATER_TEXTURE, volumes.waterTexture))
        {
            TraceLog(LOG_WARNING, "WATER: Couldn't load %s, water will be plain", WATER_TEXTURE);
            volumes.waterTexture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        }
        volumes.waterLoaded = true;
    }

    volume.surface = LoadModelFromMesh(BuildWaterMesh(volume.box));
    volume.surface.materials[0].shader = volumes.waterShader;
    volume.surface.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = volumes.waterTexture;
}

// ---------------------------------------------------------------------------------------------
// Doors

void AddDoor(Volumes &volumes, BoundingBox closedBox, Vector3 direction, float lip, float speed, float wait,
             const char *texturePath, Shader shader)
{
    Door door = {};
    door.closedBox = closedBox;
    door.speed = fmaxf(speed, 1.0f);
    door.wait = wait;
    door.state = DOOR_CLOSED;

    // Opens by its own size along the direction, minus the lip that stays in the frame, like func_door
    Vector3 dir = Vector3Normalize(direction);
    if (Vector3LengthSqr(dir) == 0.0f) dir = { 0.0f, 1.0f, 0.0f };
    Vector3 size = Vector3Subtract(closedBox.max, closedBox.min);
    float travel = fabsf(dir.x)*size.x + fabsf(dir.y)*size.y + fabsf(dir.z)*size.z - lip;
    door.openOffset = Vector3Scale(dir, fmaxf(travel, 0.0f));

    door.model = LoadModelFromMesh(GenMeshCube(size.x, size.y, size.z));
    door.model.materials[0].shader = shader;
    if (LoadGameTexture(texturePath, door.texture)) door.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = door.texture;
    else TraceLog(LOG_WARNING, "DOOR: Couldn't load the texture %s, using plain white", texturePath);
    door.material = LoadWorldMaterial(texturePath, LoadMaterialLibrary());  // Its bump, specular and gloss maps, like the world's

    volumes.doors.push_back(door);
}

static BoundingBox DoorBox(const Door &door)
{
    Vector3 offset = Vector3Scale(door.openOffset, door.amount);
    return { Vector3Add(door.closedBox.min, offset), Vector3Add(door.closedBox.max, offset) };
}

static BoundingBox Grow(BoundingBox box, float amount)
{
    return { Vector3SubtractValue(box.min, amount), Vector3AddValue(box.max, amount) };
}

static void UpdateDoor(Door &door, BoundingBox playerBox, float dt)
{
    // Walking into it opens it, even while it's closing
    bool touched = CheckCollisionBoxes(Grow(playerBox, 1.5f), DoorBox(door));
    if (touched && (door.state == DOOR_CLOSED || door.state == DOOR_CLOSING)) door.state = DOOR_OPENING;

    float length = Vector3Length(door.openOffset);
    float step = (length > 0.0f) ? door.speed*dt/length : 1.0f;

    switch (door.state)
    {
        case DOOR_OPENING:
            door.amount = fminf(door.amount + step, 1.0f);
            if (door.amount >= 1.0f)
            {
                door.state = DOOR_OPEN;
                door.waitTimer = door.wait;
            }
            break;

        case DOOR_OPEN:
            if (door.wait < 0.0f) break;  // Stays open
            door.waitTimer -= dt;
            if (door.waitTimer <= 0.0f) door.state = DOOR_CLOSING;
            break;

        case DOOR_CLOSING:
        {
            // Like func_door, a door closing on the player goes back up instead of crushing them
            float previous = door.amount;
            door.amount = fmaxf(door.amount - step, 0.0f);
            if (CheckCollisionBoxes(playerBox, DoorBox(door)))
            {
                door.amount = previous;
                door.state = DOOR_OPENING;
            }
            else if (door.amount <= 0.0f)
            {
                door.state = DOOR_CLOSED;
            }
            break;
        }

        case DOOR_CLOSED:
            break;
    }
}

void UnloadVolumes(Volumes &volumes)
{
    for (Door &door : volumes.doors)
    {
        UnloadModel(door.model);  // Leaves the shader and texture alone
        if (door.texture.id != 0) UnloadTexture(door.texture);
        UnloadWorldMaterial(door.material);
    }
    for (Volume &volume : volumes.volumes)
    {
        if (volume.surface.meshCount > 0) UnloadModel(volume.surface);
    }
    if (volumes.waterLoaded)
    {
        UnloadShader(volumes.waterShader);
        if (volumes.waterTexture.id != rlGetTextureIdDefault()) UnloadTexture(volumes.waterTexture);
    }
    for (const Soundscape &soundscape : volumes.soundscapes) StopAmbientSound(soundscape.sound);
    volumes = Volumes();
}

// ---------------------------------------------------------------------------------------------
// Soundscapes

void AddSoundscape(Volumes &volumes, BoundingBox box, Vector3 origin, const char *soundPath, float volume, float fadeDistance)
{
    Soundscape soundscape = {};
    soundscape.box = box;
    soundscape.origin = origin;
    soundscape.volume = volume;
    soundscape.fadeDistance = fadeDistance;
    soundscape.sound = (soundPath[0] != '\0') ? StartAmbientSound(soundPath) : -1;
    volumes.soundscapes.push_back(soundscape);
}

// How far inside the box a point is: the distance to its nearest side, negative outside
static float DepthInside(BoundingBox box, Vector3 p)
{
    float depth = fminf(fminf(p.x - box.min.x, box.max.x - p.x), fminf(p.y - box.min.y, box.max.y - p.y));
    return fminf(depth, fminf(p.z - box.min.z, box.max.z - p.z));
}

void UpdateSoundscapes(Volumes &volumes, Vector3 listener, float yaw)
{
    Vector3 right = { cosf(yaw), 0.0f, -sinf(yaw) };  // The same as the player's
    for (Soundscape &soundscape : volumes.soundscapes)
    {
        // Full strength from fadeDistance inside, easing out to nothing at the sides
        float depth = DepthInside(soundscape.box, listener);
        float fade = (soundscape.fadeDistance > 0.0f) ? Clamp(depth/soundscape.fadeDistance, 0.0f, 1.0f) : (depth >= 0.0f ? 1.0f : 0.0f);
        fade = fade*fade*(3.0f - 2.0f*fade);  // Smoothstep, so it doesn't start or stop abruptly
        soundscape.heardVolume = soundscape.volume*fade;

        // Panned towards the origin, flat on the ground, fully only when it's at least 32 units away to the side,
        // so standing on the origin is centered rather than flipping between ears
        Vector3 toOrigin = Vector3Subtract(soundscape.origin, listener);
        toOrigin.y = 0.0f;
        float distance = Vector3Length(toOrigin);
        float side = (distance > 0.001f) ? Vector3DotProduct(Vector3Scale(toOrigin, 1.0f/distance), right) : 0.0f;
        soundscape.heardPan = side*fminf(distance/32.0f, 1.0f)*0.8f;  // Never fully in one ear

        SetAmbientSound(soundscape.sound, soundscape.heardVolume, soundscape.heardPan);
    }
}

// ---------------------------------------------------------------------------------------------

void UpdateVolumes(Volumes &volumes, World &world, BoundingBox playerBox, float dt)
{
    world.movers.clear();
    for (Door &door : volumes.doors)
    {
        UpdateDoor(door, playerBox, dt);
        world.movers.push_back(MakeBoxBrush(DoorBox(door)));
    }

    // The first trigger_changelevel the player walks into loads its level
    if (volumes.levelChangeStarted) return;
    for (const Volume &volume : volumes.volumes)
    {
        if (volume.map.empty() || !CheckCollisionBoxes(playerBox, volume.box)) continue;
        volumes.levelChangeStarted = true;
        RequestMap(MapPath(volume.map));
        break;
    }
}

void DrawVolumes(const Volumes &volumes, const World &world, bool showAll)
{
    BeginLighting(world.lighting);
        for (const Door &door : volumes.doors)
        {
            BoundingBox box = DoorBox(door);
            SetLightingWorldMaterial(world.lighting, door.material);
            SetLightingAlphaTest(world.lighting, door.model.materialCount > 0 && IsTextureCutout(door.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture.id));  // Like a portcullis
            DrawModel(door.model, Vector3Scale(Vector3Add(box.min, box.max), 0.5f), 1.0f, WHITE);
        }
        ClearLightingMaterial(world.lighting);
        SetLightingAlphaTest(world.lighting, false);
    EndLighting();

    // Water last, since it's see-through. Both sides of each face, so the surface shows from underwater too,
    // and without writing depth, so faces of the box behind still show through the ones in front
    if (volumes.waterLoaded)
    {
        float time = (float)GetTime();
        SetShaderValue(volumes.waterShader, volumes.waterTimeLoc, &time, SHADER_UNIFORM_FLOAT);
        rlDisableBackfaceCulling();
        rlDisableDepthMask();
        for (const Volume &volume : volumes.volumes)
        {
            if (volume.surface.meshCount > 0) DrawModel(volume.surface, { 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
        }
        rlDrawRenderBatchActive();  // Draw before depth writes come back on
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
    }

    if (showAll)
    {
        for (const Volume &volume : volumes.volumes) DrawBoundingBox(volume.box, (volume.contents & CONTENTS_TRIGGER) ? ORANGE : SKYBLUE);
        for (const Door &door : volumes.doors) DrawBoundingBox(DoorBox(door), LIME);
    }
}
