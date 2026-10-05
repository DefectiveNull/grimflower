#include "raylib.h"
#include "raygui.h"
#include "binds.h"
#include "console.h"
#include "entities.h"
#include "cvars.h"
#include "game.h"
#include "gamedata.h"
#include "level.h"
#include "menu.h"
#include "player.h"
#include "settings.h"
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
static bool StartLevel(const std::string &path, Level &level, World &world, bool &worldLoaded, Player &player)
{
    Level newLevel;
    World newWorld = {};
    Player newPlayer = player;
    if (!LoadLevel(newLevel, path.c_str()) || !SpawnLevel(newLevel, newWorld, newPlayer))
    {
        ConsolePrintf("Couldn't load the level %s, see the warnings above", path.c_str());
        return false;
    }

    if (worldLoaded) UnloadWorld(world);
    level = newLevel;
    world = newWorld;
    player = newPlayer;
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

    ConsoleInit();  // Before InitWindow, so the console also shows raylib's startup messages
    InitWindow(1280, 720, GAME_NAME);
    InitAudioDevice();
    InitSettings();
    ApplyAudioSettings();

    // Load assets relative to the executable, not wherever it was launched from
    ChangeDirectory(GetApplicationDirectory());
    MountGameData("gamedata.zip");
    UseProjectFolder();  // Running from inside the project: its loose files win over the zip, so editor saves show up right away

    // Configs, in the same order as Quake's quake.rc: the default binds from the game data, then the
    // player's saved binds and cvars (written by cfg_save), then their own commands
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

    SetTargetFPS(200);
    SetExitKey(KEY_NULL); // Esc opens the pause menu instead of closing the window

    Scene scene = SCENE_MAIN_MENU;
    Level level;
    World world = {};
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
            if (StartLevel(path, level, world, worldLoaded, player))
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

        // Esc backs out of the options screen first
        bool escapePressed = !consoleFocused && IsKeyPressed(KEY_ESCAPE);
        if (escapePressed && showOptions)
        {
            showOptions = false;
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

            UpdatePlayer(player, world, dt);
        }
        else
        {
            // The console or pause menu has the controls now, so let go of any held buttons
            ReleaseBinds();
        }

        BeginDrawing();
            world.lighting.pointShadows = r_use_pointlight_shadows.value != 0.0f;
            RenderWorldShadows(world);  // Its own render pass, so it has to come before BeginMode3D
            ClearBackground(RAYWHITE);

            Camera camera = GetPlayerCamera(player);
            BeginMode3D(camera);
                DrawSkybox(world.skybox, camera);  // First, so the world draws over it
                DrawWorld(world, r_showcolliders.value != 0.0f);
                DrawLightMarkers(world.lighting);
            EndMode3D();
            DrawWorldEntityLabels(world, camera);

            // Crosshair
            DrawLine(screenWidth/2 - 8, screenHeight/2, screenWidth/2 + 8, screenHeight/2, DARKGRAY);
            DrawLine(screenWidth/2, screenHeight/2 - 8, screenWidth/2, screenHeight/2 + 8, DARKGRAY);

            DrawText("Default keys - WASD: move | Space: jump | X: fly | F1: colliders | Tab: cursor | Esc: pause | `: console", 10, 10, 20, DARKGRAY);
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

    if (worldLoaded) UnloadWorld(world);
    UnmountGameData();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
