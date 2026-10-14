#include "raylib.h"
#include "raygui.h"
#include "audio.h"
#include "bounce.h"
#include "binds.h"
#include "gamecommands.h"
#include "console.h"
#include "entities.h"
#include "cvars.h"
#include "game.h"
#include "gamedata.h"
#include "level.h"
#include "menu.h"
#include "player.h"
#include "settings.h"
#include "volumes.h"
#include "world.h"
#include "worlddebug.h"

#include <cstring>

enum Scene
{
    SCENE_MAIN_MENU,  // Menu on a plain white background, nothing from the game loaded
    SCENE_GAME
};

static const char *DEFAULT_MAP = "maps/dev.json";  // What Play on the main menu loads

static std::string requestedMap;  // Set by RequestMap, loaded at the start of the next frame

void RequestMap(const std::string &path)
{
    requestedMap = path;
}

// Loads and spawns a level in place of the current one. If it fails, the current one stays as it was
static bool StartLevel(const std::string &path, Level &level, World &world, bool &worldLoaded, Player &player, Volumes &volumes)
{
    Level newLevel;
    World newWorld = {};
    Player newPlayer = player;
    Volumes newVolumes = {};
    if (!LoadLevel(newLevel, path.c_str()) || !SpawnLevel(newLevel, newWorld, newPlayer, newVolumes))
    {
        ConsolePrintf("Couldn't load the level %s, see the warnings above", path.c_str());
        return false;
    }

    // The sun's bounce light: the lightmap the editor saved with the level. Without one, or if the world
    // model, its textures or the sun changed since it was baked, it's baked in the background and shows up
    // when it's done
    if (r_bounce.value != 0.0f)
    {
        std::string lightmapPath = LightmapPath(path);
        if (LoadLightmap(newWorld, lightmapPath.c_str(), newLevel.info.lightmapKey))
        {
            ConsolePrintf("Loaded the bounce lightmap %s", lightmapPath.c_str());
        }
        else
        {
            StartLightmapBake(newWorld);
            ConsolePrintf("No up to date bounce lightmap for this level, baking one in the background. Save the level in the editor to keep one");
        }
    }

    if (worldLoaded)
    {
        UnloadVolumes(volumes);
        UnloadWorld(world);
    }
    level = newLevel;
    world = newWorld;
    player = newPlayer;
    volumes = newVolumes;
    worldLoaded = true;

    ConsolePrintf("Loaded level \"%s\" from %s", level.info.name.c_str(), path.c_str());
    return true;
}

int main(int argc, char **argv)
{
    // game.exe -exportentities [file]: writes the entity table for the editor and quits, without
    // opening a window. build.bat runs this after every build
    if (argc >= 2 && strcmp(argv[1], "-exportentities") == 0)
    {
        const char *path = (argc >= 3) ? argv[2] : "entitylist.txt";
        return ExportEntityList(path) ? 0 : 1;
    }

    ConsoleInit(GAME_NAME);  // Before InitWindow, so the console also shows raylib's startup messages
    AddGameCommands();       // Before the configs, which bind keys to them
    InitWindow(1280, 720, GAME_NAME);
    InitAudio();  // Before the configs, which set the volumes
    InitSettings();  // Before the configs, which set the settings cvars

    // Load assets relative to the executable, not wherever it was launched from
    ChangeDirectory(GetApplicationDirectory());
    MountGameData("gamedata.zip");
    UseProjectFolder();  // Running from inside the project: its loose files win over the zip, so editor saves show up right away

    // Configs, in the same order as Quake's quake.rc: the default binds from the game data, then the
    // player's saved binds and options (written by the options menu and cfg_save), then their own commands
    ExecConfig("cfg/default.cfg", false);
    ExecConfig("config.cfg", true);
    ExecConfig("autoexec.cfg", true);

    // Then commands from the command line, like Quake's: game.exe +map platforms runs "map platforms".
    // Each + starts a new command, and the words after it are its arguments
    std::string commandLine;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '+') commandLine += (commandLine.empty() ? "" : "\n") + std::string(argv[i] + 1);
        else if (!commandLine.empty()) commandLine += " \"" + std::string(argv[i]) + "\"";
    }
    if (!commandLine.empty()) ExecuteCommandText(commandLine);

    // The configs and command line have set the options by now
    ApplyVideoSettings();

    SetTargetFPS(200);
    SetExitKey(KEY_NULL); // Esc opens the pause menu instead of closing the window

    Scene scene = SCENE_MAIN_MENU;
    Level level;
    World world = {};
    Volumes volumes = {};  // The level's water, ladders, triggers and doors
    bool worldLoaded = false;
    Player player = {};
    bool paused = false;
    bool showOptions = false;  // Options screen, opened from either the main menu or the pause menu
    bool quit = false;

    while (!quit && !WindowShouldClose())
    {
        // The resolution can change from the options screen, so read it every frame
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();

        // A level asked for by the map command or the main menu. Loading one drops you straight into it
        if (!requestedMap.empty())
        {
            std::string path = requestedMap;
            requestedMap.clear();
            if (StartLevel(path, level, world, worldLoaded, player, volumes))
            {
                scene = SCENE_GAME;
                paused = false;
                showOptions = false;
                ConsoleClose();
                DisableCursor();
            }
        }

        // While the console is open it gets the keyboard, and the game ignores input
        bool consoleFocused = ConsoleUpdate();
        UpdateAudio();  // Streams the music and applies volume changes

        // A bounce lightmap baking in the background goes into use as soon as it's done
        BounceStats bake;
        if (worldLoaded && UpdateLightmapBake(world, &bake))
        {
            ConsolePrintf("Baked the bounce lightmap (%dx%d, %d texels) in %.0f ms", bake.width, bake.height, bake.texels, bake.bakeMs);
            world.cubemapsStale = true;  // So reflections show the bounce light too
        }

        // Cubemaps are captured once, when the level loads, and again when its bounce bake finishes: lights
        // that move after that don't change what's reflected
        if (worldLoaded && world.cubemapsStale) CaptureCubemaps(world);

        // Esc backs out of the options screen first, unless it's waiting for a key to bind: then Esc cancels that
        bool escapePressed = !consoleFocused && IsKeyPressed(KEY_ESCAPE);
        if (escapePressed && showOptions)
        {
            if (!IsOptionsMenuCapturingKey()) showOptions = false;
            escapePressed = false;
        }

        if (scene == SCENE_MAIN_MENU)
        {
            BeginDrawing();
                ClearBackground(WHITE);

                // Menu buttons can't be clicked through the console
                if (consoleFocused) GuiLock();

                if (showOptions)
                {
                    if (DrawOptionsMenu()) showOptions = false;
                }
                else
                {
                    switch (DrawMainMenu(GAME_NAME))
                    {
                        case MENU_CONTINUE: RequestMap(DEFAULT_MAP); break;
                        case MENU_OPTIONS: showOptions = true; break;
                        case MENU_QUIT: quit = true; break;
                        default: break;
                    }
                }

                GuiUnlock();

                DrawFPS(screenWidth - 90, screenHeight - 30);
                ConsoleDraw();
            EndDrawing();
            continue;
        }

        // SCENE_GAME
        if (escapePressed)
        {
            paused = !paused;
            if (paused) EnableCursor();
            else DisableCursor();
        }

        if (!consoleFocused && !paused)
        {
            // Keys run whatever they're bound to (cfg/default.cfg, or the bind command)
            UpdateBinds();

            // Clamp the timestep so a long hitch can't tunnel the player through geometry
            float dt = GetFrameTime();
            if (dt > 0.05f) dt = 0.05f;

            UpdateVolumes(volumes, world, GetPlayerBox(player), dt);  // Doors first, so the player collides with where they are now
            UpdatePlayer(player, world, volumes, dt);
        }
        else
        {
            // The console or pause menu has the controls now, so let go of any held buttons
            ReleaseBinds();
        }

        // Soundscapes follow the player's ears, also while paused so they don't jump when the game resumes
        UpdateSoundscapes(volumes, GetPlayerCamera(player).position, player.yaw);

        BeginDrawing();
            world.lighting.pointShadows = r_use_pointlight_shadows.value != 0.0f;
            world.lighting.aoDirect = r_ssao_direct.value;
            world.lighting.bounceScale = (r_bounce.value != 0.0f) ? r_bounce_scale.value : 0.0f;
            RenderWorldShadows(world);  // Its own render pass, so it has to come before BeginMode3D

            Camera camera = GetPlayerCamera(player);
            RenderWorldAO(world, camera, screenWidth, screenHeight, r_ssao.value != 0.0f, r_ssao_radius.value);  // Its own passes too

            ClearBackground(RAYWHITE);
            BeginMode3D(camera);
                DrawSkybox(world.skybox, camera);  // First, so the world draws over it
                DrawWorld(world, r_showcolliders.value != 0.0f);
                DrawLightMarkers(world.lighting);
                DrawVolumes(volumes, world, r_showcolliders.value != 0.0f);
            EndMode3D();
            DrawWorldEntityLabels(world, camera);

            // Underwater, everything gets a blue tint. Water's faces aren't seen from inside it
            if (PointContents(volumes, camera.position) & CONTENTS_WATER) DrawRectangle(0, 0, screenWidth, screenHeight, Fade(Color{ 30, 80, 160, 255 }, 0.45f));

            // Crosshair
            DrawLine(screenWidth/2 - 8, screenHeight/2, screenWidth/2 + 8, screenHeight/2, DARKGRAY);
            DrawLine(screenWidth/2, screenHeight/2 - 8, screenWidth/2, screenHeight/2 + 8, DARKGRAY);

            DrawText("Default keys - WASD: move | Space: jump | Ctrl: crouch | X: fly | F1: colliders | Tab: cursor | Esc: pause | `: console", 10, 10, 20, DARKGRAY);
            if (player.flying) DrawText("FLYING (Space/Ctrl: up/down)", 10, 36, 20, MAROON);

            if (paused)
            {
                if (consoleFocused) GuiLock();

                if (showOptions)
                {
                    if (DrawOptionsMenu()) showOptions = false;
                }
                else
                {
                    switch (DrawPauseMenu())
                    {
                        case MENU_CONTINUE: paused = false; DisableCursor(); break;
                        case MENU_OPTIONS: showOptions = true; break;
                        case MENU_QUIT: quit = true; break;
                        default: break;
                    }
                }

                GuiUnlock();
            }

            DrawFPS(screenWidth - 90, screenHeight - 30);
            ConsoleDraw();
        EndDrawing();
    }

    if (worldLoaded)
    {
        UnloadVolumes(volumes);
        UnloadWorld(world);
    }
    UnmountGameData();
    CloseAudio();
    CloseWindow();
    return 0;
}
